A script, `drivers/windows/trust/install-trust.ps1`, makes a Windows machine
trust the owner's certificate authority and the signer of a package, so the
test-signed user-mode driver and helper service install with test signing off.
It handles public certificates only, checks the signer against the root it is
given, and can undo what it did.
