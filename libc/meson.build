# Minimal libc for freestanding builds
libc_inc = include_directories('.')

if meson.is_cross_build()
  libc_sources = files(
    'libc_impl.c',
    'arm_aeabi.c',
  )
else
  # Host builds use system libc
  libc_sources = []
endif
