ifeq ($(HOST_OS),linux)
  ifeq ($(TARGET_BUILD_VARIANT),user)
    WITH_DEXPREOPT_COMP ?= false
  endif
endif