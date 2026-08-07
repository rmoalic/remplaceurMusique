#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <shlwapi.h>
#include <wrl/client.h>

#include <string>
#include <vector>
#include <memory>
#include <atomic>

#include "resource.hpp"
#include "Encode.hpp"
#include "EncodeJob.hpp"
#include "WaveformExtractorJob.hpp"
#include "Win32Utils.hpp"

using Microsoft::WRL::ComPtr;

// Forward declarations
class WaveformExtractorJob;
class EncodeJob;

/**
 * @class MainWindow
 * @brief Classe principale de la fenêtre de l'application
 * 
 * Gère la création de l'interface utilisateur, le traitement des messages,
 * et l'encapsulation de l'état de l'application.
 */
class MainWindow
{
public:
    /**
     * @brief Constructeur
     * @param hInstance Instance de l'application
     */
    explicit MainWindow(HINSTANCE hInstance);
    
    /**
     * @brief Destructeur
     */
    ~MainWindow();
    
    /**
     * @brief Crée et affiche la fenêtre principale
     * @param nCmdShow Mode d'affichage de la fenêtre
     * @return true si la création a réussi
     */
    bool Create(int nCmdShow);
    
    /**
     * @brief Point d'entrée du message loop
     */
    void RunMessageLoop();
    
    /**
     * @brief Fonction de rappel statique pour le message loop
     */
    static LRESULT CALLBACK WindowProcStatic(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    
    /**
     * @brief Fonction de rappel pour la fenêtre de forme d'onde
     */
    static LRESULT CALLBACK WaveformWindowProcStatic(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    // ---------------------------------------------------------------------------
    // Types et structures
    // ---------------------------------------------------------------------------
    
    /** @brief État spécifique à l'UI, accessible uniquement par le thread UI */
    struct UIState
    {
        HWND hWnd = nullptr;
        HWND hWaveWnd = nullptr;
        Win32Utils::Font fontUI;
        Win32Utils::Font fontBold;
        Win32Utils::Font fontSm;
        Win32Utils::Brush brushBg;
        ComPtr<ITaskbarList3> pTaskbar;

        // Sélection de fichiers
        std::wstring videoPath;
        std::wstring audioPath;
        double videoDuration = 0.0;
        double audioDuration = 0.0;

        // Curseurs de la forme d'onde (thread UI uniquement)
        double audioStartSec = 0.0;
        double audioEndSec = 0.0;
        bool draggingEnd = false;

        // Forme d'onde
        std::vector<float> waveform;
        bool waveformReady = false;
    };

    struct WaveformResult
    {
        uint64_t generation;
        std::vector<float> values;
    };

    // ---------------------------------------------------------------------------
    // Constantes de couleur et de mise en page
    // ---------------------------------------------------------------------------
    static constexpr COLORREF kColorBg = RGB(244, 244, 248);
    static constexpr COLORREF kColorWaveBg = RGB(26, 26, 46);
    static constexpr COLORREF kColorCursorStart = RGB(255, 107, 107);
    static constexpr COLORREF kColorCursorEnd = RGB(80, 220, 120);
    static constexpr COLORREF kColorZone = RGB(42, 42, 78);
    static constexpr COLORREF kColorZoneSelected = RGB(60, 100, 60);
    static constexpr COLORREF kColorText = RGB(30, 30, 40);

    static constexpr int kWaveformHeight = 90;
    static constexpr int kWaveformSamples = 700;
    static constexpr int kMargin = 16;
    static constexpr int kRowHeight = 26;
    static constexpr int kWindowWidth = 680;

    // ---------------------------------------------------------------------------
    // État de l'application
    // ---------------------------------------------------------------------------
    HINSTANCE m_hInstance = nullptr;
    LANGID m_langId = MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH);
    UIState m_ui;
    
    std::atomic<bool> m_encoding{ false };
    std::unique_ptr<EncodeJob> m_encodeJob;
    WaveformExtractorJob m_waveformJob;
    
    static int s_qualityIdx;
    static AudioShortMode s_audioShortMode;
    static int s_volumePct;

    // ---------------------------------------------------------------------------
    // Gestion de la fenêtre
    // ---------------------------------------------------------------------------
    
    /**
     * @brief Fonction de rappel pour le traitement des messages
     */
    LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    
    /**
     * @brief Fonction de rappel pour la fenêtre de forme d'onde
     */
    LRESULT CALLBACK WaveformWindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    
    // ---------------------------------------------------------------------------
    // Handlers de création et destruction
    // ---------------------------------------------------------------------------
    
    /**
     * @brief Gère le message WM_CREATE
     */
    LRESULT OnCreate(HWND hWnd, WPARAM wParam, LPARAM lParam);
    
    /**
     * @brief Gère le message WM_DESTROY
     */
    void OnDestroy();

    // ---------------------------------------------------------------------------
    // Handlers de commandes
    // ---------------------------------------------------------------------------
    
    /**
     * @brief Gère le message WM_COMMAND
     */
    LRESULT OnCommand(HWND hWnd, WPARAM wParam, LPARAM lParam);
    
    /**
     * @brief Gère le clic sur le bouton Parcourir pour la vidéo
     */
    void OnBrowseVideo();
    
    /**
     * @brief Gère le clic sur le bouton Parcourir pour l'audio
     */
    void OnBrowseAudio();
    
    /**
     * @brief Gère le clic sur le bouton Remplacer l'audio
     */
    void OnReplaceAudio();
    
    /**
     * @brief Gère la modification des champs de début/fin audio
     */
    void OnAudioTimeEdit(int controlId);
    
    /**
     * @brief Gère la perte de focus des champs de chemin
     */
    void OnPathEditKillFocus(int controlId);
    
    /**
     * @brief Gère la sélection de la qualité
     */
    void OnQualitySelectionChanged();
    
    /**
     * @brief Gère la sélection du mode audio court
     */
    void OnAudioShortModeChanged();

    // ---------------------------------------------------------------------------
    // Handlers de messages personnalisés
    // ---------------------------------------------------------------------------
    
    /**
     * @brief Gère le message WM_ENCODE_PROGRESS
     */
    void OnEncodeProgress(int pct, double etaSecs);
    
    /**
     * @brief Gère le message WM_ENCODE_DONE
     */
    void OnEncodeDone(ENCODE_DONE_MSG* encMsg);
    
    /**
     * @brief Gère le message WM_WAVEFORM_READY
     */
    void OnWaveformReady(uint64_t generation, WaveformResult* result);

    // ---------------------------------------------------------------------------
    // Handlers divers
    // ---------------------------------------------------------------------------
    
    /**
     * @brief Gère le message WM_DROPFILES
     */
    void OnDropFiles(HDROP hDrop);
    
    /**
     * @brief Gère le message WM_HSCROLL
     */
    void OnHScroll(HWND hSlider);
    
    /**
     * @brief Gère le message WM_CTLCOLORSTATIC/WM_CTLCOLOREDIT
     */
    LRESULT OnControlColor(HDC hdc);
    
    /**
     * @brief Gère le message WM_ERASEBKGND
     */
    LRESULT OnEraseBackground(HDC hdc);

    // ---------------------------------------------------------------------------
    // Utilitaires de création de contrôles
    // ---------------------------------------------------------------------------
    
    /**
     * @brief Crée un label
     */
    HWND CreateLabel(const wchar_t* text, int x, int y, int w, int h, bool bold = false);
    
    /**
     * @brief Crée un champ de texte
     */
    HWND CreateEdit(int id, const wchar_t* text, int x, int y, int w, int h);
    
    /**
     * @brief Crée un bouton
     */
    HWND CreateButton(int id, const wchar_t* text, int x, int y, int w, int h, bool isDefault = false);
    
    /**
     * @brief Crée un label statique
     */
    HWND CreateStatic(int id, const wchar_t* text, int x, int y, int w, int h, HFONT hFont = nullptr);
    
    /**
     * @brief Crée une combo box
     */
    HWND CreateComboBox(int id, int x, int y, int w, int h);

    // ---------------------------------------------------------------------------
    // Gestion des fichiers
    // ---------------------------------------------------------------------------
    
    /**
     * @brief Ouvre une boîte de dialogue pour sélectionner un fichier
     */
    std::wstring BrowseFile(bool isVideo);
    
    /**
     * @brief Ouvre une boîte de dialogue pour sauvegarder un fichier
     */
    std::wstring BrowseSave(const std::wstring& defaultName);
    
    /**
     * @brief Applique un chemin vidéo
     */
    void ApplyVideoPath(const std::wstring& path);
    
    /**
     * @brief Applique un chemin audio
     */
    void ApplyAudioPath(const std::wstring& path);

    // ---------------------------------------------------------------------------
    // Utilitaires divers
    // ---------------------------------------------------------------------------
    
    /**
     * @brief Convertit des secondes en format HH:MM:SS
     */
    static std::wstring SecondsToHMS(double s);
    
    /**
     * @brief Convertit HH:MM:SS en secondes
     */
    static double HMSToSeconds(const std::wstring& t);
    
    /**
     * @brief Récupère le texte d'un contrôle
     */
    static std::wstring GetControlText(HWND hWnd, int id);
    
    /**
     * @brief Affiche une boîte de dialogue d'erreur
     */
    void ShowError(UINT msgId, UINT titleId = IDS_ERR_TITLE_VAL);
    
    /**
     * @brief Localise une erreur d'encodage
     */
    static std::wstring LocalizeEncodeError(EncodeError err);
    
    /**
     * @brief Vérifie si deux chemins pointent vers le même fichier
     */
    static bool ArePathsEqual(const std::wstring& a, const std::wstring& b);
    
    /**
     * @brief Charge une chaîne localisée
     */
    static std::wstring LoadString(UINT id);
    
    /**
     * @brief Charge une chaîne localisée avec formatage (1 paramètre)
     */
    static std::wstring FormatString(UINT id, int value);
    
    /**
     * @brief Charge une chaîne localisée avec formatage (2 paramètres)
     */
    static std::wstring FormatString2(UINT id, const wchar_t* a, const wchar_t* b);
    
    /**
     * @brief Met à jour la progression dans la barre des tâches
     */
    void UpdateTaskbarProgress(int pct);
    
    /**
     * @brief Signale la fin du traitement dans la barre des tâches
     */
    void SignalTaskbarDone();
    
    /**
     * @brief Signale une erreur dans la barre des tâches
     */
    void SignalTaskbarError();

    // ---------------------------------------------------------------------------
    // Dessin de la forme d'onde
    // ---------------------------------------------------------------------------
    
    /**
     * @brief Dessine la forme d'onde
     */
    void DrawWaveform(HWND hWnd, HDC hdc);
};
