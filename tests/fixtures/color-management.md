# Color-management fixture provenance

`tagged-display-p3.png.base64` is a 32 × 24 opaque PNG whose pixels are
Display-P3 `(255, 128, 0)` and whose 492-byte embedded ICC profile declares
Display P3. Its decoded SHA-256 is
`0f74acfb2d4720afc9a8059f4cc7b7c44b4a9afada052913c57973e2fa937da4`.

`linear-srgb.icc.base64` is a linear-transfer sRGB display profile. Its decoded
SHA-256 is
`3785f5d844a6ffbdbf26c1d8c2588262aa2337b06ee4473ba2aeb784a041e18d`.

The expected tagged-image output was independently checked with ImageMagick
7.1.2 and an ICC sRGB destination profile:

```sh
base64 -d tagged-display-p3.png.base64 > tagged-display-p3.png
base64 -d linear-srgb.icc.base64 > linear-srgb.icc
sha256sum tagged-display-p3.png linear-srgb.icc
magick tagged-display-p3.png -profile srgb.icc txt:-
```

ImageMagick reports unclipped sRGB `(274, 119, 0)`, giving the independently
fixed 8-bit expected value `(255, 119, 0)`. Applying the linear-sRGB fixture to
sRGB `(128, 128, 128)` gives the fixed encoded value `(55, 55, 55)`.
