#pragma once

// Command IDs shared by menus, accelerators, and the app. Menu/accelerator IDs
// must fit in 16 bits for WM_COMMAND.
enum : unsigned int {
    kCmdNewTab = 1001,
    kCmdCloseTab,
    kCmdCloseOthers,
    kCmdCloseAll,
    kCmdRefresh,
    kCmdUp,
    kCmdBack,
    kCmdForward,
    kCmdAddress,
    kCmdFind,
    kCmdExit,

    kCmdOpen,
    kCmdOpenWith,
    kCmdOpenExplorer,
    kCmdOpenTerminal,
    kCmdCut,
    kCmdCopy,
    kCmdPaste,
    kCmdRename,
    kCmdDelete,
    kCmdNewFolder,
    kCmdNewFile,
    kCmdSelectAll,
    kCmdInvertSelection,
    kCmdCopyPath,
    kCmdProperties,

    kCmdShowHidden,
    kCmdSortName,
    kCmdSortSize,
    kCmdSortDate,
    kCmdSortType,
    kCmdSortAscending,
    kCmdSortDescending,

    kCmdExtract,
    kCmdExtractAll,

    kCmdDesktop,
    kCmdDocuments,
    kCmdDownloads,
    kCmdPictures,
    kCmdVideos,
    kCmdHome,
    kCmdThisPc,

    kCmdNextTab,
    kCmdPrevTab,
    kCmdAbout,

    kCmdSetDefaultFileManager,
    kCmdRestoreExplorerDefault,

    // Burger-menu categories (second-level popup).
    kCmdMenuFile,
    kCmdMenuEdit,
    kCmdMenuView,
    kCmdMenuGo,
    kCmdMenuHelp,

    kCmdZoomIn,
    kCmdZoomOut,
    kCmdZoomReset,
    kCmdListFont,
    kCmdUiFont,
};
