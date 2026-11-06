Loading a topology from a directory, a device or a pipe now fails with
"invalid argument" instead of reading nothing or never finishing. The test
runner exits with an error when a JUnit report it was asked for cannot be
written, and the fuzz replay fails on an input it cannot read. The Boost
download script refuses archive members that would land outside the cache.
