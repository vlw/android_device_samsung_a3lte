# Source-built HIDL adapters for native A3 secure-world firmware.
# The Keymaster service probes the TA at runtime and self-tests it; it falls back
# to the software Keymaster when no TA works, so every A3 variant can use it.
# Selecting this does not flash firmware or enable encryption.
PRODUCT_VENDOR_PROPERTIES += ro.vendor.a3tee.enabled=1
DEVICE_KEYMASTER_SERVICE := vendor.a3tee.keymaster@3.0-service
# Not every native TA has a Gatekeeper protocol. Keep the existing software
# Gatekeeper and its SIDs. Hardware GK requires separate credential qualification.
ifeq ($(A3LTE_TEE_GATEKEEPER),true)
DEVICE_GATEKEEPER_SERVICE := vendor.a3tee.gatekeeper@1.0-service
endif
# KM3 supplies software algorithms through the existing km_compat library.
# Do not also advertise software KM4.1: Android12 includes its manifest entry
# in shared-secret negotiation, which this old backend cannot perform.
