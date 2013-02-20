# Release name
PRODUCT_RELEASE_NAME := m805_892x

# Inherit some common CM stuff.
$(call inherit-product, vendor/cm/config/common_full_tablet_wifionly.mk)

# Inherit device configuration
$(call inherit-product, device/targa/utab_7.6/device_utab_7.6.mk)

## Device identifier. This must come after all inclusions
PRODUCT_DEVICE := m805_892x
PRODUCT_NAME := cm_m805_892x
PRODUCT_BRAND := Android
PRODUCT_MODEL := Targa uTab 7.6
PRODUCT_MANUFACTURER := unknown
