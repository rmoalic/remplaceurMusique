#pragma once

#include <string>
#include <vector>
#include <cstdio>
#include <cwchar>

/**
 * @class StringUtils
 * @brief Utilitaires de manipulation de chaînes de caractères
 * 
 * Fournit des fonctions pour la conversion, le formatage et la manipulation
 * de chaînes, principalement pour l'affichage et l'interface utilisateur.
 */
class StringUtils
{
public:
    /**
     * @brief Convertit une durée en secondes au format HH:MM:SS
     * @param seconds Durée en secondes (les valeurs négatives sont mises à 0)
     * @return std::wstring Chaîne formatée au format HH:MM:SS
     */
    static std::wstring SecondsToHMS(double seconds);
    
    /**
     * @brief Convertit une chaîne au format HH:MM:SS en secondes
     * @param time Chaîne au format HH:MM:SS ou SS ou MM:SS
     * @return double Durée en secondes, ou -1.0 en cas d'erreur
     */
    static double HMSToSeconds(const std::wstring& time);
    
    /**
     * @brief Convertit une chaîne au format HH:MM:SS en millisecondes
     * @param time Chaîne au format HH:MM:SS
     * @return long long Durée en millisecondes, ou -1 en cas d'erreur
     */
    static long long HMSToMilliseconds(const std::wstring& time);
    
    /**
     * @brief Formate une durée en secondes pour l'affichage
     * @param seconds Durée en secondes
     * @return std::wstring Durée formatée de manière lisible
     */
    static std::wstring FormatDuration(double seconds);
    
    /**
     * @brief Formate une taille en octets pour l'affichage
     * @param bytes Taille en octets
     * @return std::wstring Taille formatée (Ko, Mo, Go, etc.)
     */
    static std::wstring FormatFileSize(uint64_t bytes);
    
    /**
     * @brief Remplace toutes les occurrences d'une sous-chaîne
     * @param str Chaîne originale
     * @param from Sous-chaîne à remplacer
     * @param to Nouvelle sous-chaîne
     * @return std::wstring Nouvelle chaîne avec les remplacements
     */
    static std::wstring ReplaceAll(std::wstring str, const std::wstring& from, const std::wstring& to);
    
    /**
     * @brief Convertit une chaîne en minuscules
     * @param str Chaîne à convertir
     * @return std::wstring Chaîne en minuscules
     */
    static std::wstring ToLower(const std::wstring& str);
    
    /**
     * @brief Convertit une chaîne en majuscules
     * @param str Chaîne à convertir
     * @return std::wstring Chaîne en majuscules
     */
    static std::wstring ToUpper(const std::wstring& str);
    
    /**
     * @brief Supprime les espaces de début et de fin
     * @param str Chaîne à nettoyer
     * @return std::wstring Chaîne sans espaces de début/fin
     */
    static std::wstring Trim(const std::wstring& str);
    
    /**
     * @brief Vérifie si une chaîne commence par un préfixe
     * @param str Chaîne à tester
     * @param prefix Préfixe à rechercher
     * @param caseSensitive Sensible à la casse (défaut: vrai)
     * @return bool vrai si la chaîne commence par le préfixe
     */
    static bool StartsWith(const std::wstring& str, const std::wstring& prefix, bool caseSensitive = true);
    
    /**
     * @brief Vérifie si une chaîne se termine par un suffixe
     * @param str Chaîne à tester
     * @param suffix Suffixe à rechercher
     * @param caseSensitive Sensible à la casse (défaut: vrai)
     * @return bool vrai si la chaîne se termine par le suffixe
     */
    static bool EndsWith(const std::wstring& str, const std::wstring& suffix, bool caseSensitive = true);
    
    /**
     * @brief Divise une chaîne en tokens en utilisant un délimiteur
     * @param str Chaîne à diviser
     * @param delimiter Délimiteur
     * @return std::vector<std::wstring> Vecteur de tokens
     */
    static std::vector<std::wstring> Split(const std::wstring& str, const std::wstring& delimiter);
    
    /**
     * @brief Joint des tokens en une seule chaîne
     * @param tokens Vecteur de tokens
     * @param delimiter Délimiteur
     * @return std::wstring Chaîne résultante
     */
    static std::wstring Join(const std::vector<std::wstring>& tokens, const std::wstring& delimiter);
    
    /**
     * @brief Vérifie si une chaîne est vide ou contient uniquement des espaces
     * @param str Chaîne à tester
     * @return bool vrai si la chaîne est vide ou contient uniquement des espaces
     */
    static bool IsEmptyOrWhitespace(const std::wstring& str);
    
    /**
     * @brief Obtient l'extension d'un fichier à partir de son chemin
     * @param filePath Chemin du fichier
     * @return std::wstring Extension du fichier (sans le point)
     */
    static std::wstring GetFileExtension(const std::wstring& filePath);
    
    /**
     * @brief Obtient le nom du fichier (sans le chemin) à partir de son chemin
     * @param filePath Chemin du fichier
     * @return std::wstring Nom du fichier
     */
    static std::wstring GetFileName(const std::wstring& filePath);
    
    /**
     * @brief Obtient le nom du fichier sans l'extension
     * @param filePath Chemin du fichier
     * @return std::wstring Nom du fichier sans extension
     */
    static std::wstring GetFileNameWithoutExtension(const std::wstring& filePath);
};
