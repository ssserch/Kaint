# pdn5: Paint.NET 5 plugin layer from paint.rs

This folder comes from paint.rs (`paintnetrecreation/pdn-host`, GPL-3.0-or-later; see `LICENSE`).
Kaint's .NET plugin system (`src/pdnhost5`, `PdnBridge.dll`) is built on top of it.

## Contents

- `assemblies/`: paint.rs's replacement `PaintDotNet.*` assemblies, version 5.200. They include the
  Direct2D wrappers over OpenD2D and headless stand-ins for Windows Forms, the registry, WPF and others.
  - `WindowsFramework/PdnShaders.cs` holds the bytecode of Paint.NET's own pixel shaders, copied from
    Paint.NET 5. It is kept at the owner's request.
- `bridge/`: the plugin host. It covers loading, schemas, CPU, GPU and bitmap rendering, headless
  settings windows, selections and FileTypes.
  - Kaint does not use `Exports.cs` (paint.rs's C ABI). `src/pdnhost5/Program.cs` speaks Kaint's
    JSON-lines protocol instead.
- `filetypes-host/`: two files the bridge compiles in.
- `keys/PublicKey.snk`: used to sign the `System.Windows.Extensions` stand-in.

## Changes for Kaint

- `bridge/PdnBridge.csproj`
  - Targets `net10.0` instead of `net11.0`.
  - Package versions changed from 11.0.0 to 10.0.0.
  - Project paths adjusted.
  - Only used for reference: Kaint builds `src/pdnhost5/KaintPdnHost.csproj`.
- `bridge/Gate.cs`: adds `Gate.AllowAll`. Kaint does not prompt for each permission, so gated calls are
  allowed, as with its mono host. `KAINT_PDN_GATE=1` turns the gate back on.
- `bridge/NativeResolver.cs`: also looks for OpenD2D's libraries in `KAINT_OPEND2D`.
- `bridge/PluginHost.cs`: `RenderJob.IsWhole` and `RenderJob.Parts`, so Kaint can render a classic
  effect's strips on several threads.
- `assemblies/WindowsExtensions/WindowsExtensions.csproj`: the signing key path now points to `keys/`.
