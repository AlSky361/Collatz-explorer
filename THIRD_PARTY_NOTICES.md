# Third-party notices

The original Collatz Explorer code is licensed under [BSD-3-Clause](LICENSE).
The separate notices and licenses below continue to apply to dependencies.

## nlohmann/json 3.12.0

The unmodified modular headers in `third_party/nlohmann/` are vendored from
nlohmann/json 3.12.0. Copyright (c) 2013–2025 Niels Lohmann.

- [Upstream source](https://github.com/nlohmann/json/tree/v3.12.0).
- [Full MIT license](third_party/nlohmann.LICENSE.MIT).
- [Local provenance and integrity checks](third_party/README.md).

Preserve the upstream copyright and MIT license when redistributing the headers
or a binary incorporating them. The project's BSD license does not replace them.

## Separately installed Python dependencies

NumPy, pandas, Matplotlib and SciPy are installed through `requirements.txt`;
their source code is not vendored here. They retain their own licenses and
notices. Transitive dependencies retain their own notices as well.

CMake, compilers and Python are build or runtime tools. If distributing a binary
bundle containing these or other third-party components, include the notices
applicable to the components actually bundled.
