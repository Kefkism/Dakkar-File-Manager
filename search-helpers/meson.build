libgsf = dependency('libgsf-1', required: true)

mso_to_txt_sources = [
  'nemo-mso-to-txt.c'
]

mso_to_txt = executable('dakkar-mso-to-txt',
  mso_to_txt_sources,
  dependencies: [libgsf, gio, glib],
  install: true
)

ppt_to_txt_sources = [
  'nemo-ppt-to-txt.c'
]

ppt_to_txt = executable('dakkar-ppt-to-txt',
  ppt_to_txt_sources,
  dependencies: [gio, glib],
  install: true
)

install_data(
    ['dakkar-xls-to-txt', 'dakkar-odf-to-txt'],
    install_dir: join_paths(get_option('prefix'), get_option('bindir')),
    install_mode: 'rwxr-xr-x'
)

install_data(
    'epub2text.nemo_search_helper',
    'mso.nemo_search_helper',
    'mso-ppt.nemo_search_helper',
    'mso-xls.nemo_search_helper',
    'odf.nemo_search_helper',
    install_dir: join_paths(nemoDataPath, 'search-helpers')
)

install_data(
    'dakkar-epub2text',
    install_dir: get_option('bindir')
)

subdir('third-party')