
if [ "$#" -ne 2 ] || ! [ -d "$2" ]; then
  echo "Usage: $0 IDENTITY DIRECTORY" >&2
  exit 1
fi

identity="$1"
rm -r nwjs.app
cp -R $2/nwjs.app .

codesign --force --verify --verbose --sign "$identity" nwjs.dummy.app
zip -r nwjs.dummy.zip nwjs.dummy.app/

app="nwjs.app"
version=$(ls "$app"/Contents/Frameworks/nwjs\ Framework.framework/Versions | head -1)

echo "### signing frameworks"
codesign --force --verify --verbose --sign "$identity" "$app/Contents/Frameworks/Mantle.framework/"
codesign --force --verify --verbose --sign "$identity" "$app/Contents/Frameworks/ReactiveCocoa.framework/"
codesign --force --verify --verbose --sign "$identity" "$app/Contents/Frameworks/Squirrel.framework/"
codesign --force --verify --verbose --sign "$identity" "$app/Contents/Frameworks/nwjs Framework.framework/Versions/$version/Helpers/app_mode_loader"
codesign --force --verify --verbose --sign "$identity" "$app/Contents/Frameworks/nwjs Framework.framework/Versions/$version/Helpers/chrome_crashpad_handler"
codesign --force --verify --verbose --sign "$identity" "$app/Contents/Frameworks/nwjs Framework.framework/Versions/$version/Helpers/nwjs Helper (GPU).app/"
codesign --force --verify --verbose --sign "$identity" "$app/Contents/Frameworks/nwjs Framework.framework/Versions/$version/Helpers/nwjs Helper (Plugin).app/"
codesign --force --verify --verbose --sign "$identity" "$app/Contents/Frameworks/nwjs Framework.framework/Versions/$version/Helpers/nwjs Helper (Renderer).app/"
codesign --force --verify --verbose --sign "$identity" "$app/Contents/Frameworks/nwjs Framework.framework/Versions/$version/Helpers/nwjs Helper.app/"
codesign --force --verify --verbose --sign "$identity" "$app/Contents/Frameworks/nwjs Framework.framework/Versions/$version/libffmpeg.dylib"
codesign --force --verify --verbose --sign "$identity" "$app/Contents/Frameworks/nwjs Framework.framework/Versions/$version/libnode.dylib"
codesign --force --verify --verbose --sign "$identity" "$app/Contents/Frameworks/nwjs Framework.framework/Versions/$version/XPCServices/AlertNotificationService.xpc"
codesign --force --verify --verbose --sign "$identity" "$app/Contents/Frameworks/nwjs Framework.framework/"

echo "### signing app"
codesign --force --verify --verbose --sign "$identity" "$app"

echo "### verifying signature"
codesign -vvv -d "$app"

nwjs.app/Contents/MacOS/nwjs
