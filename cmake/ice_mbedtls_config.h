// libdatachannel's DTLS implementation expects the upstream SRTP extension
// API even in a data-channel-only build. The encryption remains upstream.
#define MBEDTLS_SSL_DTLS_SRTP
// libdatachannel calls PSA from several RTC workers. Mbed TLS defaults to
// single-threaded PSA; enabling its upstream mutex layer is mandatory.
#define MBEDTLS_THREADING_C
#define MBEDTLS_THREADING_PTHREAD
