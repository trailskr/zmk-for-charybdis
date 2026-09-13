zephyr_include_directories(include)

if((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  target_sources_ifdef(CONFIG_ZMK_BEHAVIOR_RUEN app PRIVATE src/behaviors/behavior_ruen.c)
endif()
