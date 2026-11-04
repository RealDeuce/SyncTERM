import "syncterm" for Error

// Trusted main-menu data model.  This module is available only in the
// persistent menu VM; connected-session VMs cannot import or bind it.

// Existing native result: portable reason and raw integer status may be absent.
foreign class MenuError is Error {
  foreign code
  foreign nativeCode
  foreign message
  foreign toString
}

class BBSListSource {
  static personal  { 0 }
  static shared    { 1 }
  static webSystem { 2 }
  static webNamed  { 3 }
}

// Owned snapshot, copied from the current native list only when requested.
foreign class BBSLoadDiagnostic {
  foreign source
  foreign sourceName
  foreign sectionName
  foreign error
}

class Menu {
  // Transactionally replace the in-memory directory.  On failure the
  // previous generation remains active. Returns null/MenuError; password may be null.
  foreign static load(password)
  foreign static loadDiagnostics
  foreign static quickConnect(url)
  // A detached Surface snapshot, or null when no completed session exists.
  foreign static offlineScrollback
  foreign static prepareOfflineScrollback()
  foreign static offlineScrollbackHasStatus
  foreign static openUrl(url)
  foreign static openHyperlink(id)
  foreign static setHyperlinkHover(id)
  // Classic main-screen chrome supplied by the trusted host.
  foreign static applicationTitle
  foreign static timeText
  foreign static timestamp
  foreign static showEntry(entry)
  // Set the process exit latch after the trusted menu has chosen to exit.
  foreign static quitApplication()
  foreign static entries
  foreign static canAppendEntry
  // Valid root script modules available for per-entry connected loading.
  foreign static scriptModules
  // Maximum byte length of the comma-separated WrenScripts INI value.
  foreign static wrenScriptsMaxLength
  foreign static defaults
  foreign static defaultsDirty
  foreign static saveDefaults()
  foreign static settings
  foreign static nameAvailable(name)
  // create/copy insert an unsaved personal record into the working model.
  foreign static create(name)
  foreign static copy(source, name)
  foreign static sort()
  // Sort fields are [fieldId, displayName, defaultReversed].  Profiles
  // are [name, signedFieldIdList]; negating an ID toggles its direction.
  foreign static sortFields
  foreign static sortProfiles
  foreign static activeSortProfile
  foreign static saveSortProfiles()
  foreign static setActiveSortProfile(index)
  foreign static addSortProfile(index, name, order)
  foreign static updateSortProfile(index, name, order)
  foreign static deleteSortProfile(index)
  // Catalog rows are [numeric value, display name].
  foreign static connectionTypes
  foreign static defaultPort(connectionType)
  foreign static addressFamilies
  foreign static rates
  // Query a serial device's supported rates, falling back to rates when
  // the device cannot be opened or does not publish a rate catalog.
  foreign static serialRates(device)
  foreign static musicModes
  foreign static ripModes
  foreign static flowControls
  foreign static flowControlsNoRts
  foreign static parities
  // [minimumColorCount, sixteen mode-specific conio default colors].
  foreign static paletteDefaults(screenMode)
  foreign static fontsCatalog
  foreign static encodings
  foreign static fontsForEncoding(encoding)
  foreign static fontEncoding(name)
  foreign static logLevels
  foreign static screenModes
  // Each build-dependent pair is [numeric value, display name].
  foreign static outputModes
  foreign static cursorStyles
  foreign static audioModes
  // [category, display name, enabled] rows for this executable.
  foreign static buildOptions
  // [component name, full notice text] rows embedded in this executable.
  foreign static licenses
  // Build-specific maximum path length accepted by menu models.
  foreign static maxPathLength
  foreign static encryptionAvailable
  foreign static scalingModes
  foreign static colors
  foreign static backgroundColors
  foreign static currentScreenMode
  foreign static setScreenMode(mode)
  foreign static fileLocations
  // Theme rows are [filename, name, author, description, version, error].
  // An empty filename identifies built-in Classic; a non-null error makes
  // a row informational and non-selectable.
  foreign static themes
  foreign static selectedThemeFile
  foreign static selectedThemePackage
  foreign static previewTheme(filename)
  foreign static cancelThemePreview()
  foreign static selectTheme(filename)
  foreign static deleteTheme(filename)
  foreign static newThemeDocument()
  foreign static openThemeDocument(filename)
  // Cloud rows are [package, name, author, description, version,
  // cached, updateAvailable, error].
  foreign static refreshCloudThemes()
  foreign static cloudThemes
  foreign static previewCloudTheme(package)
  foreign static selectCloudTheme(package)
  foreign static deleteCloudTheme(package)
  foreign static cacheCloudTheme(package)
  foreign static copyCloudTheme(package)
  foreign static encryptionInfo
  foreign static setEncryption(algorithm, keySize, password)
  // Web-list rows are [name, URI].  Add/refresh return null on success
  // or MenuError. All action methods return null/MenuError.
  foreign static webLists
  foreign static webListsDirty
  foreign static addWebList(name, uri, index)
  foreign static updateWebList(index, uri)
  foreign static deleteWebList(index)
  foreign static saveWebLists()
  foreign static refreshWebList(index)
  // Each menu host owns an editable custom-font draft. Structural changes
  // invalidate existing MenuFont handles. saveFonts() persists the draft;
  // activateFonts() activates configured fonts; reloadFonts() rereads the draft.
  foreign static fonts
  foreign static fontsDirty
  foreign static canCreateFont
  foreign static reloadFonts()
  foreign static createFont(name, index)
  foreign static saveFonts()
  foreign static activateFonts()
}

class MenuEncryption {
  static none     { 0 }
  static tripleDes { 1 }
  static idea     { 2 }
  static cast     { 3 }
  static rc2      { 4 }
  static rc4      { 5 }
  static aes      { 6 }
  static chacha20 { 7 }
}

class ListEncryptionFormat {
  static plain { 0 }
  static v1    { 1 }
  static v2    { 2 }
}

class ListEncryptionKdf {
  static none         { 0 }
  static pbkdf2Sha256  { 1 }
  static scrypt       { 2 }
}

// Host-created, immutable copies of the loaded personal-list metadata.
foreign class ListEncryptionInfo {
  foreign format
  foreign algorithm
  foreign keyBits
  foreign name
  foreign kdf
  foreign iterations
  foreign costLog2
  foreign blockSize
  foreign parallelism
  foreign rewriteRecommended
}

class MenuFontSlot {
  static eightByEight    { 0 }
  static eightByFourteen { 1 }
  static eightBySixteen  { 2 }
  static twelveByTwenty  { 3 }
}

foreign class MenuFont {
  foreign name
  foreign name=(value)
  // path() is for display only.  setFile() requires a read-authorized
  // File returned by the trusted picker. Bitmap size is checked when loaded.
  foreign path(slot)
  foreign setFile(slot, file)
  foreign clearFile(slot)
  foreign delete()
  foreign toString
}

// Native document-save enum; portable I/O errors use IOErr instead.
class ThemeSaveErr {
  static ok       { 0 }
  static invalid  { 1 }
  static conflict { 2 }
  static exists   { 3 }
  static readOnly { 4 }
  static io       { 5 }
}

// C-owned source-preserving theme document.  Field IDs and value modes are
// documented in Themes.adoc. Mutators return null on success or
// MenuError; save() preserves its distinct native enum in nativeCode.
foreign class ThemeDocument {
  foreign filename
  foreign metadata(field)
  foreign setMetadata(field, value)
  foreign styles
  foreign setStyle(role, field, mode, value)
  foreign addStyle(role)
  foreign removeStyle(role)
  foreign glyphs
  foreign setGlyph(name, field, mode, value)
  foreign addGlyph(name, ascii)
  foreign removeGlyph(name)
  foreign themeData
  foreign dirty
  foreign canUndo
  foreign canRedo
  foreign undo()
  foreign redo()
  foreign importTheme(filename)
  foreign save(filename, force)
  foreign activate()
}

// C-owned editable snapshot of program settings.  apply() updates the
// running program; save() also writes syncterm.ini.
foreign class Settings {
  foreign dirty
  foreign apply()
  foreign save()
  foreign reload()
  foreign confirmClose
  foreign confirmClose=(value)
  foreign promptSave
  foreign promptSave=(value)
  foreign colorEmoji
  foreign colorEmoji=(value)
  foreign invertWheel
  foreign invertWheel=(value)
  foreign modemDevice
  foreign modemDevice=(value)
  foreign modemInit
  foreign modemInit=(value)
  foreign modemDial
  foreign modemDial=(value)
  foreign listPath
  foreign listPath=(value)
  foreign shellTerm
  foreign shellTerm=(value)
  foreign tlsClientCert
  foreign tlsClientCert=(value)
  foreign tlsClientKey
  foreign tlsClientKey=(value)
  foreign startupMode
  foreign startupMode=(value)
  foreign outputMode
  foreign outputMode=(value)
  foreign cursorStyle
  foreign cursorStyle=(value)
  foreign audioModes
  foreign audioModes=(value)
  foreign scrollbackLines
  foreign scrollbackLines=(value)
  foreign transferSuccessKeypressTimeout
  foreign transferSuccessKeypressTimeout=(value)
  foreign transferFailureKeypressTimeout
  foreign transferFailureKeypressTimeout=(value)
  foreign modemRate
  foreign modemRate=(value)
  foreign scalingMode
  foreign scalingMode=(value)
  foreign kdfShift
  foreign kdfShift=(value)
  foreign customRows
  foreign customRows=(value)
  foreign customColumns
  foreign customColumns=(value)
  foreign customFontHeight
  foreign customFontHeight=(value)
  foreign customAspectWidth
  foreign customAspectWidth=(value)
  foreign customAspectHeight
  foreign customAspectHeight=(value)
  foreign frameColor
  foreign frameColor=(value)
  foreign textColor
  foreign textColor=(value)
  foreign backgroundColor
  foreign backgroundColor=(value)
  foreign inverseColor
  foreign inverseColor=(value)
  foreign lightbarColor
  foreign lightbarColor=(value)
  foreign lightbarBackgroundColor
  foreign lightbarBackgroundColor=(value)
}

// Entry identity survives unrelated insertion/deletion. Reload and removal
// invalidate this entry and its profile views.
foreign class DirectoryEntry {
  foreign name
  foreign searchKey
  foreign rename(name)
  foreign type
  foreign profile
  foreign readOnly
  foreign cache
  foreign id
  foreign added
  foreign connected
  foreign calls
  foreign dirty
  foreign save()
  foreign delete()
  foreign sortOrder
  foreign sortOrder=(value)
  foreign toString
}
