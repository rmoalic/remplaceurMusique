#pragma once

#include <string>
#include <vector>
#include <memory>

// Forward declarations
struct IShellLinkW;
class StringUtils;

/**
 * @class FileUtils
 * @brief Utilitaires de gestion de fichiers et de chemins
 * 
 * Fournit des fonctions pour la manipulation de fichiers, la sélection
 * de fichiers via des boîtes de dialogue, et la vérification de chemins.
 */
class FileUtils
{
public:
    // ---------------------------------------------------------------------------
    // File Dialog Functions
    // ---------------------------------------------------------------------------
    
    /**
     * @brief Ouvre une boîte de dialogue pour sélectionner un fichier
     * @param hOwner Fenêtre parente (peut être nullptr)
     * @param title Titre de la boîte de dialogue
     * @param filter Filtre des fichiers (ex: L"Video\0*.mp4;*.avi\0All\0*.*\0")
     * @param defaultExtension Extension par défaut
     * @return std::wstring Chemin du fichier sélectionné, ou chaîne vide si annulé
     */
    static std::wstring OpenFileDialog(void* hOwner, const std::wstring& title, 
                                      const std::wstring& filter, const std::wstring& defaultExtension = L"");
    
    /**
     * @brief Ouvre une boîte de dialogue pour sélectionner un fichier vidéo
     * @param hOwner Fenêtre parente (peut être nullptr)
     * @return std::wstring Chemin du fichier vidéo sélectionné
     */
    static std::wstring BrowseVideoFile(void* hOwner);
    
    /**
     * @brief Ouvre une boîte de dialogue pour sélectionner un fichier audio
     * @param hOwner Fenêtre parente (peut être nullptr)
     * @return std::wstring Chemin du fichier audio sélectionné
     */
    static std::wstring BrowseAudioFile(void* hOwner);
    
    /**
     * @brief Ouvre une boîte de dialogue pour sauvegarder un fichier
     * @param hOwner Fenêtre parente (peut être nullptr)
     * @param defaultName Nom de fichier par défaut
     * @param filter Filtre des fichiers
     * @param defaultExtension Extension par défaut
     * @return std::wstring Chemin du fichier de destination, ou chaîne vide si annulé
     */
    static std::wstring SaveFileDialog(void* hOwner, const std::wstring& defaultName,
                                      const std::wstring& filter, const std::wstring& defaultExtension);
    
    // ---------------------------------------------------------------------------
    // File Information Functions
    // ---------------------------------------------------------------------------
    
    /**
     * @brief Vérifie si un fichier existe
     * @param filePath Chemin du fichier
     * @return bool vrai si le fichier existe
     */
    static bool FileExists(const std::wstring& filePath);
    
    /**
     * @brief Vérifie si un répertoire existe
     * @param directoryPath Chemin du répertoire
     * @return bool vrai si le répertoire existe
     */
    static bool DirectoryExists(const std::wstring& directoryPath);
    
    /**
     * @brief Obtient la taille d'un fichier en octets
     * @param filePath Chemin du fichier
     * @return uint64_t Taille du fichier en octets, ou 0 si erreur
     */
    static uint64_t GetFileSize(const std::wstring& filePath);
    
    /**
     * @brief Obtient la date de dernière modification d'un fichier
     * @param filePath Chemin du fichier
     * @return FILETIME Date de dernière modification, ou {0, 0} si erreur
     */
    static void GetFileLastWriteTime(const std::wstring& filePath, void* lastWriteTime);
    
    // ---------------------------------------------------------------------------
    // Path Manipulation Functions
    // ---------------------------------------------------------------------------
    
    /**
     * @brief Vérifie si deux chemins pointent vers le même fichier
     * @param path1 Premier chemin
     * @param path2 Deuxième chemin
     * @return bool vrai si les chemins pointent vers le même fichier
     */
    static bool ArePathsEqual(const std::wstring& path1, const std::wstring& path2);
    
    /**
     * @brief Obtient le chemin absolu d'un fichier
     * @param relativePath Chemin relatif
     * @return std::wstring Chemin absolu, ou chaîne vide si erreur
     */
    static std::wstring GetAbsolutePath(const std::wstring& relativePath);
    
    /**
     * @brief Obtient le répertoire parent d'un chemin
     * @param path Chemin du fichier ou répertoire
     * @return std::wstring Répertoire parent
     */
    static std::wstring GetParentDirectory(const std::wstring& path);
    
    /**
     * @brief Combine plusieurs composants de chemin en un seul chemin
     * @param parts Composants de chemin à combiner
     * @return std::wstring Chemin combiné
     */
    static std::wstring CombinePaths(const std::vector<std::wstring>& parts);
    
    /**
     * @brief Normalise un chemin (remplace les / par \, supprime les .\ et ..\)
     * @param path Chemin à normaliser
     * @return std::wstring Chemin normalisé
     */
    static std::wstring NormalizePath(const std::wstring& path);
    
    // ---------------------------------------------------------------------------
    // File Type Functions
    // ---------------------------------------------------------------------------
    
    /**
     * @brief Vérifie si un fichier est un fichier vidéo suppporté
     * @param filePath Chemin du fichier
     * @return bool vrai si le fichier a une extension vidéo
     */
    static bool IsVideoFile(const std::wstring& filePath);
    
    /**
     * @brief Vérifie si un fichier est un fichier audio suppporté
     * @param filePath Chemin du fichier
     * @return bool vrai si le fichier a une extension audio
     */
    static bool IsAudioFile(const std::wstring& filePath);
    
    /**
     * @brief Vérifie si un fichier est un fichier image
     * @param filePath Chemin du fichier
     * @return bool vrai si le fichier a une extension image
     */
    static bool IsImageFile(const std::wstring& filePath);
    
    /**
     * @brief Obtient le type MIME d'un fichier basé sur son extension
     * @param filePath Chemin du fichier
     * @return std::wstring Type MIME, ou chaîne vide si inconnu
     */
    static std::wstring GetMimeType(const std::wstring& filePath);
    
    // ---------------------------------------------------------------------------
    // File Operations
    // ---------------------------------------------------------------------------
    
    /**
     * @brief Copie un fichier
     * @param source Chemin du fichier source
     * @param destination Chemin du fichier de destination
     * @param overwrite Écraser si le fichier de destination existe (défaut: faux)
     * @return bool vrai si la copie a réussi
     */
    static bool CopyFile(const std::wstring& source, const std::wstring& destination, bool overwrite = false);
    
    /**
     * @brief Déplace un fichier
     * @param source Chemin du fichier source
     * @param destination Chemin du fichier de destination
     * @return bool vrai si le déplacement a réussi
     */
    static bool MoveFile(const std::wstring& source, const std::wstring& destination);
    
    /**
     * @brief Supprime un fichier
     * @param filePath Chemin du fichier à supprimer
     * @return bool vrai si la suppression a réussi
     */
    static bool DeleteFile(const std::wstring& filePath);
    
    /**
     * @brief Crée un répertoire (et tous les répertoires parents si nécessaire)
     * @param directoryPath Chemin du répertoire à créer
     * @return bool vrai si la création a réussi
     */
    static bool CreateDirectory(const std::wstring& directoryPath);
    
    // ---------------------------------------------------------------------------
    // Temporary Files
    // ---------------------------------------------------------------------------
    
    /**
     * @brief Crée un fichier temporaire avec une extension spécifique
     * @param extension Extension du fichier temporaire
     * @return std::wstring Chemin du fichier temporaire, ou chaîne vide si erreur
     */
    static std::wstring CreateTempFile(const std::wstring& extension = L"tmp");
    
    /**
     * @brief Obtient le répertoire des fichiers temporaires de l'utilisateur
     * @return std::wstring Chemin du répertoire temporaire
     */
    static std::wstring GetTempDirectory();
    
    // ---------------------------------------------------------------------------
    // Shortcut Functions
    // ---------------------------------------------------------------------------
    
    /**
     * @brief Crée un raccourci vers un fichier
     * @param shortcutPath Chemin où créer le raccourci
     * @param targetPath Chemin cible du raccourci
     * @param description Description du raccourci
     * @return bool vrai si le raccourci a été créé
     */
    static bool CreateShortcut(const std::wstring& shortcutPath, const std::wstring& targetPath, 
                               const std::wstring& description = L"");
    
    /**
     * @brief Obtient la cible d'un raccourci
     * @param shortcutPath Chemin du raccourci
     * @return std::wstring Chemin cible, ou chaîne vide si erreur
     */
    static std::wstring GetShortcutTarget(const std::wstring& shortcutPath);
};
