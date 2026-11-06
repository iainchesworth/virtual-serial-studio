**Cleaned up three static-analysis findings in the listen transport.** A read
rejected because another was already pending no longer uses a moved-from
handler, the write path no longer copies the payload pointer twice, and the
destructor no longer lets an exception from `close()` escape.
