# Dakkar: gettext catalogues. The domain stays 'nemo' (GETTEXT_PACKAGE) so
# translations installed for stock Nemo strings keep working; nl.po carries
# the Dakkar strings on top. 'ninja nemo-pot' regenerates po/nemo.pot and
# 'ninja nemo-update-po' merges it into every language in LINGUAS.
i18n = import('i18n')
i18n.gettext('nemo',
  preset : 'glib',
  args : [
    '--from-code=UTF-8',
    '--add-comments=Translators',
    '--keyword=ngettext:1,2',
    '--package-name=Dakkar',
  ],
)
