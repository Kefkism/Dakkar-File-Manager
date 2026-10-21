
################################################################################
dataConf = configuration_data()
dataConf.set('VERSION', meson.project_version())

desktopFiles = [
  configure_file(
    input : 'nemo-autorun-software.desktop.in',
    output: 'nemo-autorun-software.desktop',
    configuration: dataConf,
  ),

  configure_file(
    input : 'nemo-autostart.desktop.in',
    output: 'nemo-autostart.desktop',
    configuration: dataConf,
  ),

  configure_file(
    input : 'nemo.desktop.in',
    output: 'nemo.desktop',
    configuration: dataConf,
  )
]

# The Nemo desktop launchers are not installed: Dakkar ships its own
# desktop entry and must not replace the system Nemo launcher.
# install_data(desktopFiles, ...) is disabled for this fork.

################################################################################
dataConf = configuration_data()
dataConf.set('bindir', join_paths(get_option('prefix'), get_option('bindir')))

# Dakkar's own launcher (not the system nemo.desktop): org.dakkar.Files.desktop
configure_file(
  input : 'org.dakkar.Files.desktop.in',
  output: 'org.dakkar.Files.desktop',
  configuration: dataConf,
  install_dir: join_paths(get_option('datadir'), 'applications'),
)

dbusFiles = [
  configure_file(
    input : 'nemo.service.in',
    output: 'nemo.service',
    configuration: dataConf,
  ),

  configure_file(
    input : 'nemo.FileManager1.service.in',
    output: 'nemo.FileManager1.service',
    configuration: dataConf,
  ),
]

# The original org.Nemo and FileManager1 D-Bus activation services belong
# to the distro's installed Nemo, not this Dakkar application.

################################################################################

# Internal Nemo-specific MIME and polkit definitions are not exported by Dakkar.
# Installing them on the host would alter system-wide file associations.

subdir('icons')

# AppStream metainfo for the Dakkar launcher (icon: org.dakkar.Files)
install_data('org.dakkar.Files.metainfo.xml',
  install_dir: join_paths(get_option('datadir'), 'metainfo'),
)
