#include "ble_companion.h"

#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#include <algorithm>
#include <atomic>
#include <cstring>

#include "app_settings.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

extern "C" void ble_store_config_init(void);

namespace ble_companion {
namespace {

constexpr char kTag[] = "ble";
constexpr size_t kDashMax = 2048;

#define OTP_UUID(n) BLE_UUID128_INIT(0x6d, 0x5c, 0x4b, 0x3a, 0x2e, 0x1f, 0x6d, 0x9c, \
                                     0x7a, 0x4b, 0x0f, 0x9e, (n), 0x00, 0xc5, 0xb1)

const ble_uuid128_t kSvcUuid = OTP_UUID(0x00);
const ble_uuid128_t kSettingsUuid = OTP_UUID(0x01);
const ble_uuid128_t kStatusUuid = OTP_UUID(0x02);
const ble_uuid128_t kDashUuid = OTP_UUID(0x09);

uint16_t s_settingsHandle;
uint16_t s_statusHandle;
uint16_t s_dashHandle;

uint8_t s_ownAddrType;
std::atomic<uint16_t> s_conn{BLE_HS_CONN_HANDLE_NONE};
std::atomic<uint32_t> s_pairCode{0};
std::atomic<int64_t> s_pairCodeAtUs{0};
std::atomic<bool> s_settingsChanged{false};

uint8_t s_status[10];

char s_dashText[kDashMax];
size_t s_dashLen = 0;
char s_dashPending[kDashMax];
char s_dashRx[kDashMax];
size_t s_dashRxLen = 0;
std::atomic<bool> s_dashApply{false};

void fill_settings(uint8_t* buf)
{
    const int16_t ftp = static_cast<int16_t>(app_settings::ftpWatts());
    const int16_t tz = static_cast<int16_t>(app_settings::tzMinutes());
    std::memcpy(buf, &ftp, 2);
    std::memcpy(buf + 2, &tz, 2);
    buf[4] = app_settings::useMiles();
    buf[5] = static_cast<uint8_t>(app_settings::backlight());
    buf[6] = app_settings::clock24h();
    buf[7] = app_settings::usbDrive();
}

int append(ble_gatt_access_ctxt* ctxt, const void* data, size_t len)
{
    return os_mbuf_append(ctxt->om, data, len) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

int settings_access(uint16_t, uint16_t, ble_gatt_access_ctxt* ctxt, void*)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t buf[8];
        fill_settings(buf);
        return append(ctxt, buf, sizeof(buf));
    }
    uint8_t v[16];
    uint16_t n = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, v, sizeof(v), &n) != 0 || n < 4) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    app_settings::setFtpWatts(static_cast<int16_t>(v[0] | (v[1] << 8)));
    app_settings::setTzMinutes(static_cast<int16_t>(v[2] | (v[3] << 8)));
    if (n >= 6) {
        app_settings::setUseMiles(v[4] != 0);
        app_settings::setBacklight(v[5]);
    }
    if (n >= 7) app_settings::setClock24h(v[6] != 0);
    if (n >= 8) app_settings::setUsbDrive(v[7] != 0);
    s_settingsChanged = true;
    return 0;
}

int status_access(uint16_t, uint16_t, ble_gatt_access_ctxt* ctxt, void*)
{
    return append(ctxt, s_status, sizeof(s_status));
}

int dash_access(uint16_t, uint16_t, ble_gatt_access_ctxt* ctxt, void*)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) return append(ctxt, s_dashText, s_dashLen);

    // Chunked: [0x01] begin, [0x02 bytes] chunk, [0x03] commit. A write that
    // starts with a printable byte is the legacy whole-text path.
    static uint8_t v[512];
    uint16_t n = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, v, sizeof(v), &n) != 0 || n == 0) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    if (v[0] == 0x01) {
        s_dashRxLen = 0;
    } else if (v[0] == 0x02) {
        const size_t room = kDashMax - 1 - s_dashRxLen;
        const size_t take = std::min<size_t>(n - 1, room);
        std::memcpy(s_dashRx + s_dashRxLen, v + 1, take);
        s_dashRxLen += take;
    } else if (v[0] == 0x03) {
        s_dashRx[s_dashRxLen] = 0;
        std::memcpy(s_dashPending, s_dashRx, s_dashRxLen + 1);
        s_dashRxLen = 0;
        s_dashApply = true;
    } else if (v[0] >= 0x20) {
        std::memcpy(s_dashPending, v, n);
        s_dashPending[n] = 0;
        s_dashApply = true;
    }
    return 0;
}

const ble_gatt_chr_def kChars[] = {
    {.uuid = &kSettingsUuid.u,
     .access_cb = settings_access,
     .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
     .val_handle = &s_settingsHandle},
    {.uuid = &kStatusUuid.u,
     .access_cb = status_access,
     .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
     .val_handle = &s_statusHandle},
    {.uuid = &kDashUuid.u,
     .access_cb = dash_access,
     .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
     .val_handle = &s_dashHandle},
    {0},
};

const ble_gatt_svc_def kServices[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &kSvcUuid.u, .characteristics = kChars},
    {0},
};

void advertise();

int gap_event(ble_gap_event* event, void*)
{
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            ESP_LOGI(kTag, "connect status=%d", event->connect.status);
            if (event->connect.status == 0) {
                s_conn = event->connect.conn_handle;
                ble_gap_security_initiate(event->connect.conn_handle);
            } else {
                advertise();
            }
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(kTag, "disconnect reason=%d", event->disconnect.reason);
            s_conn = BLE_HS_CONN_HANDLE_NONE;
            s_pairCode = 0;
            advertise();
            break;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            advertise();
            break;
        case BLE_GAP_EVENT_ENC_CHANGE:
            ESP_LOGI(kTag, "link encryption status=%d", event->enc_change.status);
            s_pairCode = 0;
            break;
        case BLE_GAP_EVENT_PASSKEY_ACTION:
            if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
                ble_sm_io pk = {};
                pk.action = BLE_SM_IOACT_DISP;
                pk.passkey = esp_random() % 1000000;
                s_pairCodeAtUs = esp_timer_get_time();
                s_pairCode = pk.passkey;
                ESP_LOGI(kTag, "pairing code %06lu", static_cast<unsigned long>(pk.passkey));
                ble_sm_inject_io(event->passkey.conn_handle, &pk);
            }
            break;
        case BLE_GAP_EVENT_REPEAT_PAIRING: {
            // Phone lost its bond or we did: forget the old one and pair afresh.
            ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
                ble_store_util_delete_peer(&desc.peer_id_addr);
            }
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(kTag, "mtu=%u", event->mtu.value);
            break;
        default:
            break;
    }
    return 0;
}

void advertise()
{
    ble_hs_adv_fields fields = {};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = const_cast<ble_uuid128_t*>(&kSvcUuid);
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    if (ble_gap_adv_set_fields(&fields) != 0) return;

    ble_hs_adv_fields rsp = {};
    const char* name = ble_svc_gap_device_name();
    rsp.name = reinterpret_cast<const uint8_t*>(name);
    rsp.name_len = static_cast<uint8_t>(std::strlen(name));
    rsp.name_is_complete = 1;
    if (ble_gap_adv_rsp_set_fields(&rsp) != 0) return;

    ble_gap_adv_params params = {};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    const int rc = ble_gap_adv_start(s_ownAddrType, nullptr, BLE_HS_FOREVER, &params, gap_event, nullptr);
    if (rc != 0 && rc != BLE_HS_EALREADY) ESP_LOGW(kTag, "adv start rc=%d", rc);
}

void on_sync()
{
    ble_hs_id_infer_auto(0, &s_ownAddrType);
    advertise();
    ESP_LOGI(kTag, "advertising as OpenTrailPaper");
}

void host_task(void*)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

}  // namespace

void begin()
{
    s_status[0] = 0;
    if (nimble_port_init() != ESP_OK) {
        ESP_LOGE(kTag, "nimble_port_init failed");
        return;
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("OpenTrailPaper");
    ble_att_set_preferred_mtu(247);
    if (ble_gatts_count_cfg(kServices) != 0 || ble_gatts_add_svcs(kServices) != 0) {
        ESP_LOGE(kTag, "GATT registration failed");
        return;
    }
    ble_store_config_init();
    nimble_port_freertos_init(host_task);
}

bool phoneConnected() { return s_conn.load() != BLE_HS_CONN_HANDLE_NONE; }

uint32_t pairingCode()
{
    // An abandoned phone dialog never completes; don't leave the sheet up forever.
    if (s_pairCode.load() && esp_timer_get_time() - s_pairCodeAtUs.load() > 60'000'000) s_pairCode = 0;
    return s_pairCode.load();
}

void setStatus(const Status& s)
{
    const uint16_t sp = static_cast<uint16_t>(s.speedKmh * 10);
    const uint16_t rk = static_cast<uint16_t>(s.remainKm * 10);
    s_status[0] = s.flags;
    s_status[1] = s.battery;
    s_status[2] = s.sats;
    s_status[3] = s.hr;
    s_status[4] = s.power & 0xFF;
    s_status[5] = s.power >> 8;
    s_status[6] = sp & 0xFF;
    s_status[7] = sp >> 8;
    s_status[8] = rk & 0xFF;
    s_status[9] = rk >> 8;
    if (phoneConnected()) ble_gatts_chr_updated(s_statusHandle);
}

void settingsChangedOnDevice()
{
    if (phoneConnected()) ble_gatts_chr_updated(s_settingsHandle);
}

bool takeSettingsChanged() { return s_settingsChanged.exchange(false); }

void setDashText(const char* text, size_t len)
{
    s_dashLen = std::min(len, kDashMax - 1);
    std::memcpy(s_dashText, text, s_dashLen);
}

bool takeDashText(char* out, size_t cap)
{
    if (!s_dashApply.exchange(false)) return false;
    std::strncpy(out, s_dashPending, cap - 1);
    out[cap - 1] = 0;
    return true;
}

}  // namespace ble_companion
