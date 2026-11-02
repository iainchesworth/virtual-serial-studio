Add explicit GUI presets and fail loudly without Qt

The existing presets enabled the GUI but only found Qt through QT_ROOT;
when it was unset the configure succeeded with the GUI silently skipped, so
the result looked like a CLI-only build.

Add debug-gui and release-gui presets that set VSS_GUI_REQUIRED, and make a
missing Qt a configure error with the path to fix it. The default presets keep
the old fallback. Document QT_ROOT and the preset matrix in the README.
