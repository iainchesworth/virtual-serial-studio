`create_com0com_pair` and `remove_com0com_pair` now verify that `setupc.exe`
exists before handing it to `ShellExecuteEx`. A missing binary made the shell
block on its own error dialog, which hung the Windows test suite when the
configured path was deliberately bogus.
