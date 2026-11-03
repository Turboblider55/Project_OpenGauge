# Mit csinál: legenerálja az ékezetes (Latin-1 + Latin Extended-A) kiegészítő betűkészleteket a Montserrat-Medium.ttf-ből.
# Hogyan:     az lv_font_conv (Node) a kért karaktertartományokból LVGL-betűkészlet C forrást készít méretenként;
#             a kiegészítő betűkészlet tartaléka (fallback) a beépített Montserrat, ezért az ASCII jelek és a
#             LV_SYMBOL_* szimbólumok onnan jönnek, itt csak a hiányzó karakterek vannak.
# Miért:      a beépített Montserrat csak ASCII-t tartalmaz; a magyar ékezetes betűk (ő, ű stb.) és a zeneszámok
#             nemzetközi ékezetei így jeleníthetők meg.
# Használat:  powershell -ExecutionPolicy Bypass -File generate.ps1   (Node.js szükséges; az első futás letölti az lv_font_conv-ot)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

# Latin-1 kiegészítés + Latin Extended-A, és néhány tipográfiai jel: kötőjelek, idézőjelek, három pont, felsorolásjel.
$range = '0xA0-0x17F,0x2013,0x2014,0x2018,0x2019,0x201C,0x201D,0x2026,0x2022'

foreach ($size in 20, 28) {
    npx --yes lv_font_conv@1.5.3 --bpp 4 --size $size --font Montserrat-Medium.ttf -r $range `
        --format lvgl --no-compress --no-prefilter --lv-include lvgl.h `
        --lv-font-name "og_font_montserrat_$size" --lv-fallback "lv_font_montserrat_$size" `
        -o "og_font_montserrat_$size.c"
    if ($LASTEXITCODE -ne 0) { throw "A generálás sikertelen ($size px)" }
}
