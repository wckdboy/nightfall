
These compile-time contracts prevent a context from moving to another OS thread or
being shared by reference between threads, including after a clone.

```compile_fail,E0277
use nightfall_native::Context;
fn requires_send<T: Send>() {}
requires_send::<Context>();
```

```compile_fail,E0277
use nightfall_native::Context;
fn requires_sync<T: Sync>() {}
requires_sync::<Context>();
```
