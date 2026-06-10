# Build / Compile / Linker Errors

## Common UE5-specific causes

- **Unresolved external symbol** — missing module in `.Build.cs`. Add the module to `PublicDependencyModuleNames`.
- **"Cannot open include file"** — module not added to Build.cs, or wrong include path. UE5 uses `#include "Module/Class.h"` not relative paths.
- **Circular includes** — A.h includes B.h includes A.h. Use forward declarations in headers, full includes in .cpp only.
- **`GENERATED_BODY()` missing** — any UCLASS/USTRUCT/UENUM without it won't compile. Cryptic errors about missing constructors.
- **`LNK2005: already defined`** — function defined in header without `inline` or `FORCEINLINE`. Move implementation to .cpp.
- **Variable named `Tags` in AActor subclass** — shadows `AActor::Tags`, compiler error C4458. Use `ActorTags` or a more specific name.
- **Missing `#include "MyClass.generated.h"`** — must be the LAST include in the header. Generates reflection code.
- **`No matching function for call to 'ProcessEvent'`** — wrong UFUNCTION signature. Check parameter types match the delegate signature.
- **Hot reload corruption** — `UHT` (Unreal Header Tool) state gets stale. Close editor, delete `Intermediate/`, rebuild.

## Investigation steps

1. Read the FIRST error — later errors are usually cascading from the first
2. If it's a linker error, check Build.cs modules
3. If it's a missing include, add the module to Build.cs THEN add the include
4. When in doubt: close editor, delete Intermediate folder, full rebuild
