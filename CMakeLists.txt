# Custom behaviors only run on the central (left) half, where the keymap lives.
if((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  target_sources(app PRIVATE src/behaviors/behavior_chord_hold.c)
endif()
