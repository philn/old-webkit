/*
 *  Copyright (C) 2019 Igalia S.L. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"

#if ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)

#include "GStreamerWebRTCUtils.h"
#include "RTCIceCandidate.h"
#include "RTCIceProtocol.h"

#include <openssl/bn.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>
#include <wtf/CryptographicallyRandomNumber.h>
#include <wtf/Scope.h>
#include <wtf/WallTime.h>
#include <wtf/text/Base64.h>
#include <wtf/text/StringToIntegerConversion.h>

GST_DEBUG_CATEGORY_STATIC(webkit_webrtc_utils_debug);
#define GST_CAT_DEFAULT webkit_webrtc_utils_debug

namespace WebCore {

static inline RTCIceComponent toRTCIceComponent(int component)
{
    return component == 1 ? RTCIceComponent::Rtp : RTCIceComponent::Rtcp;
}

static inline std::optional<RTCIceProtocol> toRTCIceProtocol(const String& protocol)
{
    if (!protocol || protocol.isEmpty())
        return {};
    if (protocol == "udp")
        return RTCIceProtocol::Udp;
    ASSERT(protocol == "tcp");
    return RTCIceProtocol::Tcp;
}

static inline std::optional<RTCIceTcpCandidateType> toRTCIceTcpCandidateType(const String& type)
{
    if (!type || type.isEmpty())
        return {};
    if (type == "active")
        return RTCIceTcpCandidateType::Active;
    if (type == "passive")
        return RTCIceTcpCandidateType::Passive;
    ASSERT(type == "so");
    return RTCIceTcpCandidateType::So;
}

static inline std::optional<RTCIceCandidateType> toRTCIceCandidateType(const String& type)
{
    if (!type || type.isEmpty())
        return {};
    if (type == "host")
        return RTCIceCandidateType::Host;
    if (type == "srflx")
        return RTCIceCandidateType::Srflx;
    if (type == "prflx")
        return RTCIceCandidateType::Prflx;
    ASSERT(type == "relay");
    return RTCIceCandidateType::Relay;
}

static void ensureDebugCategoryInitialized()
{
    static std::once_flag onceFlag;
    std::call_once(onceFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_webrtc_utils_debug, "webkitwebrtcutils", 0, "WebKit WebRTC utilities");
    });
}

std::optional<RTCIceCandidate::Fields> parseIceCandidateSDP(const String& sdp)
{
    ensureDebugCategoryInitialized();
    GST_DEBUG("Parsing ICE Candidate: %s", sdp.utf8().data());
    if (!sdp.startsWith("candidate:"))
        return { };

    String foundation;
    unsigned componentId = 0;
    String transport;
    unsigned priority = 0;
    String address;
    uint32_t port = 0;
    String type;
    String tcptype;
    String relatedAddress;
    guint16 relatedPort = 0;
    String usernameFragment;
    auto tokens = sdp.convertToASCIILowercase().substring(10).split(' ');

    for (auto it = tokens.begin(); it != tokens.end(); ++it) {
        auto i = std::distance(tokens.begin(), it);
        auto token = *it;
        switch (i) {
        case 0:
            foundation = token;
            break;
        case 1:
            if (auto value = parseInteger<unsigned>(token))
                componentId = *value;
            else
                return { };
            break;
        case 2:
            transport = token;
            break;
        case 3:
            if (auto value = parseInteger<unsigned>(token))
                priority = *value;
            else
                return { };
            break;
        case 4:
            address = token;
            break;
        case 5:
            if (auto value = parseInteger<unsigned>(token))
                port = *value;
            else
                return { };
            break;
        default:
            if (it + 1 == tokens.end())
                return { };

            it++;
            if (token == "typ")
                type = *it;
            else if (token == "raddr")
                relatedAddress = *it;
            else if (token == "rport")
                relatedPort = parseInteger<unsigned>(*it).value_or(0);
            else if (token == "tcptype")
                tcptype = *it;
            else if (token == "ufrag")
                usernameFragment = *it;
            break;
        }
    }

    if (type.isEmpty())
        return { };

    RTCIceCandidate::Fields fields;
    fields.foundation = foundation;
    fields.component = toRTCIceComponent(componentId);
    fields.priority = priority;
    fields.protocol = toRTCIceProtocol(transport);
    if (!address.isEmpty()) {
        fields.address = address;
        fields.port = port;
    }
    fields.type = toRTCIceCandidateType(type);
    fields.tcpType = toRTCIceTcpCandidateType(tcptype);
    if (!relatedAddress.isEmpty()) {
        fields.relatedAddress = relatedAddress;
        fields.relatedPort = relatedPort;
    }
    fields.usernameFragment = usernameFragment;
    return fields;
}

static String x509Serialize(X509* x509)
{
    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio)
        return {};

    auto scopeExit = WTF::makeScopeExit([&] {
        BIO_free(bio);
    });

    if (!PEM_write_bio_X509(bio, x509))
        return {};

    Vector<char> buffer;
    buffer.reserveCapacity(4096);
    int length = BIO_read(bio, buffer.data(), 4096);
    if (!length)
        return {};

    return String(buffer.data(), length);
}

static String privateKeySerialize(EVP_PKEY* privateKey)
{
    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio)
        return {};

    auto scopeExit = WTF::makeScopeExit([&] {
        BIO_free(bio);
    });

    if (!PEM_write_bio_PrivateKey(bio, privateKey, nullptr, nullptr, 0, nullptr, nullptr))
        return {};

    Vector<char> buffer;
    buffer.reserveCapacity(4096);
    int length = BIO_read(bio, buffer.data(), 4096);
    if (!length)
        return {};

    return String(buffer.data(), length);
}

std::optional<Ref<RTCCertificate>> generateCertificate(Ref<SecurityOrigin>&& origin, const PeerConnectionBackend::CertificateInformation& info)
{
    ensureDebugCategoryInitialized();
    EVP_PKEY* privateKey = EVP_PKEY_new();
    if (!privateKey) {
        GST_WARNING("Failed to create private key");
        return {};
    }

    auto scopeExit = WTF::makeScopeExit([&] {
        EVP_PKEY_free(privateKey);
    });

    switch (info.type) {
    case PeerConnectionBackend::CertificateInformation::Type::ECDSAP256: {
        EC_KEY* ecKey = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
        // Ensure curve name is included when EC key is serialized.
        // Without this call, OpenSSL versions before 1.1.0 will create
        // certificates that don't work for TLS.
        // This is a no-op for BoringSSL and OpenSSL 1.1.0+
        EC_KEY_set_asn1_flag(ecKey, OPENSSL_EC_NAMED_CURVE);
        if (!privateKey || !ecKey || !EC_KEY_generate_key(ecKey) || !EVP_PKEY_assign_EC_KEY(privateKey, ecKey)) {
            EC_KEY_free(ecKey);
            return {};
        }
        break;
    }
    case PeerConnectionBackend::CertificateInformation::Type::RSASSAPKCS1v15: {
        RSA* rsa = RSA_new();
        if (!rsa)
            return {};

        BIGNUM* exponent = BN_new();
        if (!BN_set_word(exponent, info.rsaParameters->publicExponent)
            || !RSA_generate_key_ex(rsa, info.rsaParameters->modulusLength, exponent, nullptr)
            || !EVP_PKEY_assign_RSA(privateKey, rsa)) {
            BN_free(exponent);
            RSA_free(rsa);
        }
        BN_free(exponent);
        break;
    }
    }

    X509* x509 = X509_new();
    if (!x509) {
        GST_WARNING("Failed to create certificate");
        return {};
    }

    auto certScopeExit = WTF::makeScopeExit([&] {
        X509_free(x509);
    });

    X509_set_version(x509, 2);

    // Set a random 64 bit integer as serial number.
    BIGNUM* serialNumber = BN_new();
    BN_pseudo_rand(serialNumber, 64, 0, 0);
    ASN1_INTEGER* asn1SerialNumber = X509_get_serialNumber(x509);
    BN_to_ASN1_INTEGER(serialNumber, asn1SerialNumber);
    BN_free(serialNumber);

    // Set a random 8 byte base64 string as issuer/subject.
    X509_NAME* name = X509_NAME_new();
    Vector<uint8_t> buffer;
    buffer.reserveInitialCapacity(8);
    WTF::cryptographicallyRandomValues(buffer.data(), 8);
    auto commonName = base64EncodeToString(buffer);
    X509_NAME_add_entry_by_NID(name, NID_commonName, MBSTRING_ASC, (const guchar*)commonName.ascii().data(), -1, -1, 0);
    X509_set_subject_name(x509, name);
    X509_set_issuer_name(x509, name);
    X509_NAME_free(name);

    // Fallback to 30 days, max out at one year.
    uint64_t expires = info.expires.value_or(2592000);
    expires = MIN(expires, 31536000000);
    X509_gmtime_adj(X509_getm_notBefore(x509), 0);
    X509_gmtime_adj(X509_getm_notAfter(x509), expires);
    X509_set_pubkey(x509, privateKey);

    if (!X509_sign(x509, privateKey, EVP_sha256())) {
        GST_WARNING("Failed to sign certificate");
        return {};
    }

    auto pem = x509Serialize(x509);
    GST_DEBUG("Generated certificate PEM: %s", pem.ascii().data());
    auto serializedPrivateKey = privateKeySerialize(privateKey);
    Vector<RTCCertificate::DtlsFingerprint> fingerprints;
    // FIXME: Fill fingerprints.
    auto expirationTime = WTF::WallTime::now().secondsSinceEpoch() + WTF::Seconds(expires);
    return RTCCertificate::create(WTFMove(origin), expirationTime.milliseconds(), WTFMove(fingerprints), WTFMove(pem), WTFMove(serializedPrivateKey));
}

bool sdpMediaHasAttributeKey(const GstSDPMedia* media, const char* key)
{
    for (unsigned i = 0; i < gst_sdp_media_attributes_len(media); i++) {
        const auto* attribute = gst_sdp_media_get_attribute(media, i);
        if (!g_strcmp0(attribute->key, key))
            return true;
    }

    return false;
}

} // namespace WebCore

#endif
