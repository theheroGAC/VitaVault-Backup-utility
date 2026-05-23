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
*   **FTP Integration:** Built-in FTP server to manage your files from a PC and optional FTP client for remote uploads.
*   **Safety Checks:** Integrated logic to prevent accidental selection of critical system partitions as backup targets.

## Controls

### Main Menu
*   **UP / DOWN**: Navigate through backup entries.
*   **CIRCLE (O)**: Toggle the selected entry.
*   **CROSS (X)**: Start the backup process (includes a free space check).
*   **SQUARE ([])**: Change the **Source Path** for the selected entry (e.g., custom folder selection).
*   **SELECT**: Cycle through profiles (`NONE`, `MINIMAL`, `NORMAL`, `COMPLETE`).
*   **TRIANGLE (▲)**: Open the backup manager list.
*   **START**: Start the internal FTP Server.

### Key Combos
*   **SELECT + SQUARE ([])**: Change the **Global Destination** (where all backups are saved).
*   **SELECT + TRIANGLE (▲)**: Reset Global Destination to default (`ux0:data/VitaVault`).

### During Operations
*   **CIRCLE (O) (Hold)**: Safely cancel an ongoing backup.

### File Browser
*   **CROSS (X)**: Enter a folder or select a partition.
*   **CIRCLE (O)**: Go back or cancel.
*   **TRIANGLE (▲)**: Create a new folder (e.g., BACKUP1, BACKUP2).
*   **START**: Confirm the current directory as the selected path.

### Backup Manager (Triangle Menu)
*   **CROSS (X)**: View details of the selected backup.
    *   *Inside Details*: **CROSS (X)** to Restore or **SELECT** to Delete.
*   **CIRCLE (O)**: Return to main menu.

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