# Cross-cutting framework services (logging now; configuration / DI / options
# later), modeled on .NET's Microsoft.Extensions.*. Each extension is flat-
# namespaced (mnemos::logging, not mnemos::extensions::logging) for ergonomic
# call sites -- the folder is an organizational grouping; see ADR 0009.
add_subdirectory(logging)
