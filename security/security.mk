# Source-built HIDL adapters for qualified native A3 firmware.
# Deliberately opt-in until hardware tests and credential migration are qualified.
# Selecting this does not flash firmware or enable encryption. The service also
# requires qualified native TA/common-library hashes; it never overrides firmware.
ifeq ($(A3LTE_TEE_SECURITY),true)
PRODUCT_VENDOR_PROPERTIES += ro.vendor.a3tee.enabled=1
DEVICE_KEYMASTER_SERVICE := vendor.a3tee.keymaster@3.0-service
# A300F BRH1's native TA has no Gatekeeper protocol. Keep the existing software
# Gatekeeper and its SIDs. Hardware GK requires separate credential qualification.
ifeq ($(A3LTE_TEE_GATEKEEPER),true)
DEVICE_GATEKEEPER_SERVICE := vendor.a3tee.gatekeeper@1.0-service
endif
# KM3 supplies software algorithms through the existing km_compat library.
# Do not also advertise software KM4.1: Android12 includes its manifest entry
# in shared-secret negotiation, which this old backend cannot perform.
endif
