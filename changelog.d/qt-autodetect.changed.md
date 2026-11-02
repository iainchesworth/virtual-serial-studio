Auto-detect Qt 6 like the other checkouts

Qt's config package is still tried first, so CMAKE_PREFIX_PATH and Qt6_DIR
win. When that fails, cmake/FindQt6.cmake searches QT_ROOT, C:/Qt,
C:/Repos/common/qt, ~/Qt, /opt/Qt and /usr/local/Qt for the kit matching the
compiler, then retries the search. The *-gui presets still fail loudly when
nothing is found.
