Import("env")
import os

APP_BIN = "$BUILD_DIR/${PROGNAME}.bin"

PROJECT_DIR = os.path.abspath(env.subst("$PROJECT_DIR"))
REPO_DIR = os.path.abspath(os.path.join(PROJECT_DIR, "../.."))
FIRMWARE_DIR = os.path.join(REPO_DIR, "firmware")

os.makedirs(FIRMWARE_DIR, exist_ok=True)

MERGED_BIN = os.path.join(FIRMWARE_DIR, "${PIOENV}.factory.bin")
OTA_BIN = os.path.join(FIRMWARE_DIR, "${PIOENV}.ota.bin")

BOARD_CONFIG = env.BoardConfig()

def merge_bin(source, target, env):
    # The list contains all extra images (bootloader, partitions, eboot) and
    # the final application binary
    flash_images = env.Flatten(env.get("FLASH_EXTRA_IMAGES", [])) + ["$ESP32_APP_OFFSET", APP_BIN]

    # Run esptool to merge images into a single binary
    env.Execute(
        " ".join(
            [
                "$PYTHONEXE",
                "$OBJCOPY",
                "--chip",
                BOARD_CONFIG.get("build.mcu", "esp32"),
                "merge_bin",
#                "--fill-flash-size",
#                BOARD_CONFIG.get("upload.flash_size", "4MB"),
                "--output",
                MERGED_BIN,
            ]
            + flash_images
        )
    )

def bin_map_copy(source, target, env):
    env.Execute(
        " ".join(
            [
                "cp",
                APP_BIN,
                OTA_BIN
            ]
        )
    )
# Add a post action that runs esptoolpy to merge available flash images
env.AddPostAction(APP_BIN, merge_bin)
env.AddPostAction(APP_BIN, bin_map_copy)

# Patch the upload command to flash the merged binary at address 0x0
# Patch the upload command to flash the merged factory binary
# using the explicit write_flash operation required by esptool 4.x.
env.Replace(
    UPLOADERFLAGS=[
        "write_flash",
        "0x0",
        MERGED_BIN,
    ],
    UPLOADCMD='"$PYTHONEXE" "$UPLOADER" $UPLOADERFLAGS',
)
