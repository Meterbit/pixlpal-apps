// The firmware's build settings that My Training's OAuth kit reads (sdkconfig). The account server refreshes Strava's
// tokens with the secret it holds. This is its address while it runs on workers.dev, as the phone app's default
// (PXP_OAUTH_BROKER_URL): change both, and Busy Light's, when the final domain is chosen.
#pragma once
#define CONFIG_PXP_OAUTH_BROKER_URL "https://pixlpal-accounts.meterbit.workers.dev"
