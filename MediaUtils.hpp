#pragma once

#include <string>
#include <cstdint>

/**
 * @class MediaUtils
 * @brief Utilitaires pour la gestion des médias
 * 
 * Fournit des fonctions pour l'interaction avec les fichiers multimédias,
 * l'extraction d'informations, et la manipulation des formats.
 */
class MediaUtils
{
public:
    // ---------------------------------------------------------------------------
    // Media Duration Functions
    // ---------------------------------------------------------------------------
    
    /**
     * @brief Obtient la durée d'un fichier multimédia en secondes
     * @param filePath Chemin du fichier multimédia
     * @return double Durée en secondes, ou 0.0 si erreur
     */
    static double GetMediaDuration(const std::wstring& filePath);
    
    // ---------------------------------------------------------------------------
    // Media Information Functions
    // ---------------------------------------------------------------------------
    
    /**
     * @brief Obtient les dimensions d'une vidéo
     * @param filePath Chemin du fichier vidéo
     * @param width Largeur de la vidéo (sortie)
     * @param height Hauteur de la vidéo (sortie)
     * @return bool vrai si les dimensions ont été obtenues
     */
    static bool GetVideoDimensions(const std::wstring& filePath, int& width, int& height);
    
    /**
     * @brief Obtient le nombre de canaux audio d'un fichier
     * @param filePath Chemin du fichier audio
     * @return int Nombre de canaux, ou 0 si erreur
     */
    static int GetAudioChannels(const std::wstring& filePath);
    
    /**
     * @brief Obtient la fréquence d'échantillonnage d'un fichier audio
     * @param filePath Chemin du fichier audio
     * @return int Fréquence d'échantillonnage en Hz, ou 0 si erreur
     */
    static int GetAudioSampleRate(const std::wstring& filePath);
    
    /**
     * @brief Obtient le débit binaire d'un fichier audio
     * @param filePath Chemin du fichier audio
     * @return int Débit binaire en bits par seconde, ou 0 si erreur
     */
    static int GetAudioBitRate(const std::wstring& filePath);
    
    // ---------------------------------------------------------------------------
    // Media Format Functions
    // ---------------------------------------------------------------------------
    
    /**
     * @brief Vérifie si un fichier est un fichier multimédia valide
     * @param filePath Chemin du fichier
     * @return bool vrai si le fichier est un fichier multimédia valide
     */
    static bool IsValidMediaFile(const std::wstring& filePath);
    
    /**
     * @brief Obtient le type de conteneur multimédia
     * @param filePath Chemin du fichier
     * @return std::wstring Type de conteneur ("mp4", "avi", etc.), ou chaîne vide si inconnu
     */
    static std::wstring GetMediaContainerFormat(const std::wstring& filePath);
    
    /**
     * @brief Obtient le codec vidéo d'un fichier
     * @param filePath Chemin du fichier vidéo
     * @return std::wstring Nom du codec vidéo, ou chaîne vide si erreur
     */
    static std::wstring GetVideoCodec(const std::wstring& filePath);
    
    /**
     * @brief Obtient le codec audio d'un fichier
     * @param filePath Chemin du fichier
     * @return std::wstring Nom du codec audio, ou chaîne vide si erreur
     */
    static std::wstring GetAudioCodec(const std::wstring& filePath);
    
    // ---------------------------------------------------------------------------
    // Media Thumbnail Functions
    // ---------------------------------------------------------------------------
    
    /**
     * @brief Extrait une vignette d'une vidéo
     * @param videoPath Chemin du fichier vidéo
     * @param outputPath Chemin où sauvegarder la vignette
     * @param width Largeur de la vignette
     * @param height Hauteur de la vignette
     * @param position Position en secondes pour l'extraction
     * @return bool vrai si la vignette a été extraite
     */
    static bool ExtractVideoThumbnail(const std::wstring& videoPath, const std::wstring& outputPath,
                                     int width = 128, int height = 128, double position = 1.0);
    
    // ---------------------------------------------------------------------------
    // Media Metadata Functions
    // ---------------------------------------------------------------------------
    
    /**
     * @brief Obtient les métadonnées d'un fichier multimédia
     * @param filePath Chemin du fichier multimédia
     * @param metadata Map pour stocker les métadonnées (clé: nom, valeur: valeur)
     * @return bool vrai si les métadonnées ont été obtenues
     */
    static bool GetMediaMetadata(const std::wstring& filePath, std::wstring& title, 
                                  std::wstring& artist, std::wstring& album);
    
    // ---------------------------------------------------------------------------
    // Media Conversion Utilities
    // ---------------------------------------------------------------------------
    
    /**
     * @brief Convertit un format vidéo en un autre
     * @param inputPath Chemin du fichier d'entrée
     * @param outputPath Chemin du fichier de sortie
     * @param outputFormat Format de sortie ("mp4", "avi", etc.)
     * @param quality Qualité (0-100)
     * @return bool vrai si la conversion a réussi
     */
    static bool ConvertVideo(const std::wstring& inputPath, const std::wstring& outputPath,
                             const std::wstring& outputFormat, int quality = 85);
    
    /**
     * @brief Extrait la piste audio d'une vidéo
     * @param videoPath Chemin du fichier vidéo
     * @param outputPath Chemin du fichier audio de sortie
     * @param format Format audio de sortie ("mp3", "wav", etc.)
     * @return bool vrai si l'extraction a réussi
     */
    static bool ExtractAudio(const std::wstring& videoPath, const std::wstring& outputPath,
                             const std::wstring& format);
};
