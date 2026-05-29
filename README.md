# VitaVault

**VitaVault** is an all-in-one backup utility for the PlayStation Vita, designed to make data preservation simple, fast, and highly customizable. Whether you want to back up save games, trophies, licenses, or specific custom folders, VitaVault provides the tools you need with a native GPU-accelerated interface.

## ⚠️ DISCLAIMER

**USE THIS TOOL AT YOUR OWN RISK.**

VitaVault is installed as an **UNSAFE** homebrew and requires extended permissions to access system partitions (such as `ur0:`, `vd0:`, `tm0:`, etc.). Modifying or restoring system files carries a risk of bricking your console or losing sensitive data. 

The author is not responsible for any damage to your hardware or software, data loss, or "bricks" resulting from the use of this application. Always ensure you have a fallback and understand what you are backing up or restoring.

## Features

*   **Recursive Backup Engine:** Copy entire directories while handling system read/write permissions.
*   **Dynamic Destination:** Choose where to save your data (ux0, uma0, ur0, etc.) via an integrated file browser.
*   **Backup Profiles:** Quickly toggle between Minimal, Normal, and Complete backup sets.
*   **Management Suite:** View, restore, or delete old backups directly from the app.
*   **FTP Server:** Built-in FTP server (port 1337) to download backups to your PC.
*   **PC Downloader:** Included `download_backup.bat` script to automatically download backups via FTP.
*   **Safety Checks:** Integrated logic to prevent accidental selection of critical system partitions as backup targets.

## Controls

### Main Menu
*   **UP / DOWN**: Navigate through backup entries.
*   **CIRCLE (O)**: Toggle the selected entry.
*   **CROSS (X)**: Start the backup process (includes a free space check).
*   **SQUARE ([])**: Change the **Source Path** for the selected entry (e.g., custom folder selection).
*   **SELECT**: Open Settings menu (toggle FTP, compression, checksum, profiles, start FTP server).
*   **TRIANGLE (▲)**: Open the backup manager list.
*   **START**: Start the internal FTP Server (port 1337).

### Key Combos
*   **SQUARE ([]) (from menu)**: Change the **Global Destination** (where all backups are saved).
*   **SELECT + SQUARE ([])**: Change the source path for the selected entry.
*   **SELECT + TRIANGLE (▲)**: Reset Global Destination to default (`ux0:data/VitaVault`).
*   **SELECT + DOWN (▼)**: Cycle through backup profiles quickly.

### During Operations
*   **CIRCLE (O) (Hold)**: Safely cancel an ongoing backup.

### File Browser
*   **CROSS (X)**: Enter a folder or select a partition.
*   **CIRCLE (O)**: Go back or cancel.
*   **TRIANGLE (▲)**: Create a new folder (e.g., BACKUP1, BACKUP2).
*   **START**: Confirm the current directory as the selected path.

### Backup Manager (Triangle Menu)
*   **CROSS (X)**: View details of the selected backup.
    *   *Inside Details*: **CROSS (X)** to Restore or **SELECT** to Delete (permanently).
*   **CIRCLE (O)**: Return to main menu.

### Settings (SELECT button)
*   **Automatic FTP Upload**: When enabled, the "Backup Completed" screen shows the option to start the FTP server for PC download.
*   **Backup Compression (ZIP)**: Compress backup data into ZIP files.
*   **Integrity Check (MD5)**: Generate MD5 checksums for backup verification.
*   **Backup Profile**: Cycle through NONE, MINIMAL, NORMAL, COMPLETE.
*   **Start FTP Server (Manual)**: Start the FTP server directly from settings.

## How to download backups to your PC

### Prerequisites on PC
- **wget for Windows**: Download from [eternallybored.org/misc/wget/](https://eternallybored.org/misc/wget/)
- Place `wget.exe` in `C:\Windows\System32\` or in the same folder as the .bat file

### Step-by-step

1. **On PS Vita**: Run a backup with **X**.
2. **On PS Vita**: On the "Backup Completed!" screen, press **START**.
   - The built-in FTP server starts (port 1337).
   - The Vita screen shows the IP address and the backup folder name.
3. **On PC**: Edit `download_backup.bat` and set `VITA_IP` to your Vita's IP address.
4. **On PC**: Double-click `download_backup.bat`.

### `download_backup.bat` Menu

```
Vita IP : 192.168.1.18
Port    : 1337
Remote  : /ux0:data/VitaVault
Save to : C:\VitaVault_Backups

1) Download ALL backups
2) Download today's backup only
3) Download specific backup (enter name)
4) Download with Windows FTP
5) Change Vita IP
6) Change remote folder (Vita path)
7) Change local folder (PC path)
8) Exit
```

| Option | Description |
|--------|-------------|
| **1** | Downloads **all** backup folders from the Vita to your PC |
| **2** | Automatically detects today's backup folder and downloads it |
| **3** | Enter a specific backup folder name manually (e.g. `2026-05-29_18-00-00`) |
| **4** | Opens the Windows built-in FTP client for manual browsing/download |
| **5** | Change the Vita IP address |
| **6** | Change the remote folder path (if you changed backup destination on Vita) |
| **7** | Change the local folder where backups are saved on PC |
| **8** | Exit |

### Changing the backup destination on Vita

You can change where backups are stored on the Vita by pressing **SQUARE ([])** from the main menu and selecting a new destination (e.g. `ux0:Backup1`, `uma0:VitaVault`, etc.).

When you change this, also update the **remote folder** in `download_backup.bat` (option 6) to match the new path on the Vita.

### Download to another drive on PC

Use option **7 (Change local folder)** to set any path, for example:
- `D:\VitaVault_Backups`
- `E:\Backups\PSVita`
- `F:\`

### FTP Server Details

- **Port**: 1337
- **User**: anonymous
- **Password**: vita@ftp
- The FTP server is read-only for the Vita's filesystem (ux0:, ur0:, uma0:)

## Credits

This tool was built thanks to the incredible work of the PS Vita scene:

*   **TheFlow**: For *HENkaku*, *VitaShell*, *Adrenaline*, and the foundations of modern Vita homebrew.
*   **Rinnegatamante**: For *vita2d* and his tireless contributions to the scene's libraries and plugins.
*   **VitaSDK Contributors**: For providing the toolchain and headers necessary for homebrew development.
*   **The PS Vita Community**: For keeping this amazing handheld alive.

## License

This project is licensed under the **GNU General Public License v3.0**. 
See the [LICENSE](LICENSE) file for the full text.

---
*Developed with passion for the PS Vita.*