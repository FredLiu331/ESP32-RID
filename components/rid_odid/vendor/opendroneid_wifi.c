#include "opendroneid.h"

#include <errno.h>
#include <string.h>
#include <time.h>

#define IEEE80211_FTYPE_MGMT 0x0000U
#define IEEE80211_STYPE_BEACON 0x0080U
#define IEEE80211_CAPINFO_SHORT_PREAMBLE 0x0020U
#define IEEE80211_CAPINFO_SHORT_SLOTTIME 0x0400U

struct __attribute__((packed)) ieee80211_mgmt {
    uint16_t frame_control;
    uint16_t duration;
    uint8_t da[6];
    uint8_t sa[6];
    uint8_t bssid[6];
    uint16_t seq_ctrl;
};

struct __attribute__((packed)) ieee80211_beacon {
    uint64_t timestamp;
    uint16_t beacon_interval;
    uint16_t capability;
};

struct __attribute__((packed)) ieee80211_vendor {
    uint8_t element_id;
    uint8_t length;
    uint8_t oui[3];
    uint8_t oui_type;
};

struct __attribute__((packed)) odid_service_info {
    uint8_t message_counter;
};

static int append_bytes(uint8_t *buf, size_t *len, size_t capacity,
                        const void *data, size_t size) {
    if (size > capacity - *len) return -ENOMEM;
    if (size != 0) memcpy(buf + *len, data, size);
    *len += size;
    return 0;
}

int odid_message_build_pack(const ODID_UAS_Data *uas_data, void *pack, size_t buflen) {
    if (!uas_data || !pack) return -EINVAL;

    ODID_MessagePack_data data;
    ODID_MessagePack_encoded encoded;
    ODID_BasicID_encoded basic;
    ODID_Location_encoded location;
    ODID_Auth_encoded auth;
    ODID_SelfID_encoded self_id;
    ODID_System_encoded system;
    ODID_OperatorID_encoded operator_id;
    odid_initMessagePackData(&data);
    for (int i = 0; i < ODID_BASIC_ID_MAX_MESSAGES; ++i) {
        if (uas_data->BasicIDValid[i]) {
            if (data.MsgPackSize >= ODID_PACK_MAX_MESSAGES) return -EINVAL;
            if (encodeBasicIDMessage(&basic, &uas_data->BasicID[i]) != ODID_SUCCESS)
                return -EINVAL;
            memcpy(&data.Messages[data.MsgPackSize], &basic, ODID_MESSAGE_SIZE);
            ++data.MsgPackSize;
        }
    }
    if (uas_data->LocationValid) {
        if (data.MsgPackSize >= ODID_PACK_MAX_MESSAGES) return -EINVAL;
        if (encodeLocationMessage(&location, &uas_data->Location) != ODID_SUCCESS)
            return -EINVAL;
        memcpy(&data.Messages[data.MsgPackSize], &location, ODID_MESSAGE_SIZE);
        ++data.MsgPackSize;
    }
    for (int i = 0; i < ODID_AUTH_MAX_PAGES; ++i) {
        if (uas_data->AuthValid[i]) {
            if (data.MsgPackSize >= ODID_PACK_MAX_MESSAGES) return -EINVAL;
            if (encodeAuthMessage(&auth, &uas_data->Auth[i]) != ODID_SUCCESS)
                return -EINVAL;
            memcpy(&data.Messages[data.MsgPackSize], &auth, ODID_MESSAGE_SIZE);
            ++data.MsgPackSize;
        }
    }
    if (uas_data->SelfIDValid) {
        if (data.MsgPackSize >= ODID_PACK_MAX_MESSAGES) return -EINVAL;
        if (encodeSelfIDMessage(&self_id, &uas_data->SelfID) != ODID_SUCCESS)
            return -EINVAL;
        memcpy(&data.Messages[data.MsgPackSize], &self_id, ODID_MESSAGE_SIZE);
        ++data.MsgPackSize;
    }
    if (uas_data->SystemValid) {
        if (data.MsgPackSize >= ODID_PACK_MAX_MESSAGES) return -EINVAL;
        if (encodeSystemMessage(&system, &uas_data->System) != ODID_SUCCESS)
            return -EINVAL;
        memcpy(&data.Messages[data.MsgPackSize], &system, ODID_MESSAGE_SIZE);
        ++data.MsgPackSize;
    }
    if (uas_data->OperatorIDValid) {
        if (data.MsgPackSize >= ODID_PACK_MAX_MESSAGES) return -EINVAL;
        if (encodeOperatorIDMessage(&operator_id, &uas_data->OperatorID) != ODID_SUCCESS)
            return -EINVAL;
        memcpy(&data.Messages[data.MsgPackSize], &operator_id, ODID_MESSAGE_SIZE);
        ++data.MsgPackSize;
    }
    if (data.MsgPackSize == 0) return -EINVAL;

    const size_t size = sizeof(encoded) -
                        (ODID_PACK_MAX_MESSAGES - data.MsgPackSize) * ODID_MESSAGE_SIZE;
    if (size > buflen) return -ENOMEM;
    if (encodeMessagePack(&encoded, &data) != ODID_SUCCESS) return -EINVAL;
    memcpy(pack, &encoded, size);
    return (int)size;
}

int odid_wifi_build_message_pack_beacon_frame(const ODID_UAS_Data *uas_data, const char *mac,
                                              const char *ssid, size_t ssid_len,
                                              uint16_t interval_tu, uint8_t send_counter,
                                              uint8_t *buf, size_t buf_size) {
    static const uint8_t broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    static const uint8_t oui[3] = {0xfa, 0x0b, 0xbc};
    if (!uas_data || !mac || !buf || (!ssid && ssid_len != 0) || ssid_len > 32) return -EINVAL;

    size_t len = 0;
    struct ieee80211_mgmt mgmt = {0};
    mgmt.frame_control = IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_BEACON;
    memcpy(mgmt.da, broadcast, sizeof(mgmt.da));
    memcpy(mgmt.sa, mac, sizeof(mgmt.sa));
    memcpy(mgmt.bssid, mac, sizeof(mgmt.bssid));
    if (append_bytes(buf, &len, buf_size, &mgmt, sizeof(mgmt)) < 0) return -ENOMEM;

    struct ieee80211_beacon beacon = {0};
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        beacon.timestamp = (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
    beacon.beacon_interval = interval_tu;
    beacon.capability = IEEE80211_CAPINFO_SHORT_PREAMBLE | IEEE80211_CAPINFO_SHORT_SLOTTIME;
    if (append_bytes(buf, &len, buf_size, &beacon, sizeof(beacon)) < 0) return -ENOMEM;

    const uint8_t ssid_ie[2] = {0x00, (uint8_t)ssid_len};
    if (append_bytes(buf, &len, buf_size, ssid_ie, sizeof(ssid_ie)) < 0 ||
        append_bytes(buf, &len, buf_size, ssid, ssid_len) < 0) return -ENOMEM;
    const uint8_t rates_ie[] = {0x01, 0x01, 0x8c};
    if (append_bytes(buf, &len, buf_size, rates_ie, sizeof(rates_ie)) < 0) return -ENOMEM;

    const size_t vendor_length_offset = len + 1;
    struct ieee80211_vendor vendor = {0xdd, 0, {0xfa, 0x0b, 0xbc}, 0x0d};
    if (append_bytes(buf, &len, buf_size, &vendor, sizeof(vendor)) < 0) return -ENOMEM;
    struct odid_service_info service = {send_counter};
    if (append_bytes(buf, &len, buf_size, &service, sizeof(service)) < 0) return -ENOMEM;
    const int pack_len = odid_message_build_pack(uas_data, buf + len, buf_size - len);
    if (pack_len < 0) return pack_len;
    len += (size_t)pack_len;
    const size_t vendor_payload_len = sizeof(oui) + 1 + sizeof(service) + (size_t)pack_len;
    if (vendor_payload_len > 255) return -ENOMEM;
    buf[vendor_length_offset] = (uint8_t)vendor_payload_len;
    return (int)len;
}
