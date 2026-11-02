/* Copy to inc/wifi_secrets.h, which is gitignored, and fill in. */
#ifndef WIFI_SECRETS_H
#define WIFI_SECRETS_H

#define WIFI_SSID     "your-network"
#define WIFI_PASSWORD "your-password"

/* WPA3-Personal. Remove this line for a WPA2-only network. */
#define WIFI_AUTH     CYW43_AUTH_WPA3_SAE_AES_PSK

#endif
