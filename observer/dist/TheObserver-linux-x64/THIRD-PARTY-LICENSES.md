# Third-party components

| Component | License | Use |
|---|---|---|
| [volk](https://github.com/zeux/volk) | MIT | Vulkan function loading |
| [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | Apache-2.0 | API headers |
| [GLFW](https://www.glfw.org/) | zlib/libpng | window + input (desktop builds) |
| [stb](https://github.com/nothings/stb) (stb_image, stb_image_write, stb_truetype) | MIT / public domain | image IO, font baking |
| [miniaudio](https://miniaud.io/) | MIT-0 / public domain | audio playback + spatialization |
| [nlohmann/json](https://github.com/nlohmann/json) | MIT | content and save data |
| [DejaVu fonts](https://dejavu-fonts.github.io/) | DejaVu license (Bitstream Vera + public-domain additions; free to redistribute) | UI typefaces |

Vendored single-file libraries live in `third_party/` with their license
headers intact. GLFW and Vulkan-Headers are consumed via system packages or
CMake FetchContent at build time.

Generated content (textures, portraits, key art, voice) was produced with
Higgsfield generative services under the account holder's license; the
procedural audio in `assets/audio` is original synthesis
(`tools/gen_audio.py`).
