SET(VERSION_MAJOR "5")
SET(VERSION_MINOR "14")
SET(VERSION_PATCH "0")
SET(VERSION_DATE "2026-04-08")

# POBsoft (1985-2026): identify the unofficial Android import test host.
# Keep the official desktop version unchanged on this Android-only branch.
IF(QT_ANDROID)
  SET(VERSION_PATCH "1")
  SET(VERSION_TAIL "-pob220-import-ui-fix")
ENDIF()
