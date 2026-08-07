#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <memory>

// Forward declarations
struct ITaskbarList3;

/**
 * @class Win32Utils
 * @brief Utilitaires spécifiques à Win32
 * 
 * Fournit des fonctions et des classes RAII pour la gestion des ressources
 * Windows (polices, pinceaux, icônes) et l'interaction avec le système.
 */
class Win32Utils
{
public:
    // ---------------------------------------------------------------------------
    // RAII Wrappers for Win32 Resources
    // ---------------------------------------------------------------------------
    
    /**
     * @class Font
     * @brief Wrapper RAII pour les polices GDI (HFONT)
     */
    class Font
    {
    public:
        Font(HFONT hFont = nullptr);
        ~Font();
        
        // Empêche la copie
        Font(const Font&) = delete;
        Font& operator=(const Font&) = delete;
        
        // Permet le déplacement
        Font(Font&& other) noexcept;
        Font& operator=(Font&& other) noexcept;
        
        HFONT Get() const;
        HFONT Release();
        void Reset(HFONT hFont = nullptr);
        
        operator HFONT() const;
        
    private:
        HFONT m_hFont;
    };
    
    /**
     * @class Brush
     * @brief Wrapper RAII pour les pinceaux GDI (HBRUSH)
     */
    class Brush
    {
    public:
        Brush(HBRUSH hBrush = nullptr);
        ~Brush();
        
        Brush(const Brush&) = delete;
        Brush& operator=(const Brush&) = delete;
        
        Brush(Brush&& other) noexcept;
        Brush& operator=(Brush&& other) noexcept;
        
        HBRUSH Get() const;
        HBRUSH Release();
        void Reset(HBRUSH hBrush = nullptr);
        
        operator HBRUSH() const;
        
    private:
        HBRUSH m_hBrush;
    };
    
    /**
     * @class Pen
     * @brief Wrapper RAII pour les stylos GDI (HPEN)
     */
    class Pen
    {
    public:
        Pen(HPEN hPen = nullptr);
        ~Pen();
        
        Pen(const Pen&) = delete;
        Pen& operator=(const Pen&) = delete;
        
        Pen(Pen&& other) noexcept;
        Pen& operator=(Pen&& other) noexcept;
        
        HPEN Get() const;
        HPEN Release();
        void Reset(HPEN hPen = nullptr);
        
        operator HPEN() const;
        
    private:
        HPEN m_hPen;
    };
    
    /**
     * @class Bitmap
     * @brief Wrapper RAII pour les bitmaps GDI (HBITMAP)
     */
    class Bitmap
    {
    public:
        Bitmap(HBITMAP hBitmap = nullptr);
        ~Bitmap();
        
        Bitmap(const Bitmap&) = delete;
        Bitmap& operator=(const Bitmap&) = delete;
        
        Bitmap(Bitmap&& other) noexcept;
        Bitmap& operator=(Bitmap&& other) noexcept;
        
        HBITMAP Get() const;
        HBITMAP Release();
        void Reset(HBITMAP hBitmap = nullptr);
        
        operator HBITMAP() const;
        
    private:
        HBITMAP m_hBitmap;
    };
    
    /**
     * @class DC
     * @brief Wrapper RAII pour les contextes de périphérique (HDC)
     */
    class DC
    {
    public:
        DC(HDC hDC = nullptr, bool manage = true);
        ~DC();
        
        DC(const DC&) = delete;
        DC& operator=(const DC&) = delete;
        
        DC(DC&& other) noexcept;
        DC& operator=(DC&& other) noexcept;
        
        HDC Get() const;
        HDC Release();
        void Reset(HDC hDC = nullptr);
        
        operator HDC() const;
        
    private:
        HDC m_hDC;
        bool m_manage;
    };
    
    // ---------------------------------------------------------------------------
    // Taskbar Utilities
    // ---------------------------------------------------------------------------
    
    /**
     * @brief Initialise l'interface ITaskbarList3
     * @return ITaskbarList3* Pointeur vers l'interface, ou nullptr si échec
     */
    static ITaskbarList3* InitializeTaskbarInterface();
    
    /**
     * @brief Libère l'interface ITaskbarList3
     * @param pTaskbar Pointeur vers l'interface à libérer
     */
    static void ReleaseTaskbarInterface(ITaskbarList3* pTaskbar);
    
    /**
     * @brief Met à jour la progression dans la barre des tâches
     * @param hWnd Fenêtre associée
     * @param pTaskbar Interface de la barre des tâches
     * @param current Progression actuelle
     * @param total Progression totale
     */
    static void SetTaskbarProgress(HWND hWnd, ITaskbarList3* pTaskbar, ULONGLONG current, ULONGLONG total);
    
    /**
     * @brief Définit l'état de la barre des tâches
     * @param hWnd Fenêtre associée
     * @param pTaskbar Interface de la barre des tâches
     * @param state État à définir (TBPF_NORMAL, TBPF_ERROR, etc.)
     */
    static void SetTaskbarState(HWND hWnd, ITaskbarList3* pTaskbar, TBPFLAG state);
    
    // ---------------------------------------------------------------------------
    // Window Utilities
    // ---------------------------------------------------------------------------
    
    /**
     * @brief Centrer une fenêtre par rapport à une autre fenêtre ou à l'écran
     * @param hWnd Fenêtre à centrer
     * @param hParent Fenêtre parente (nullptr pour centrer sur l'écran)
     */
    static void CenterWindow(HWND hWnd, HWND hParent = nullptr);
    
    /**
     * @brief Obtient les dimensions de l'écran de travail (sans la barre des tâches)
     * @return RECT Dimensions de l'écran de travail
     */
    static RECT GetWorkArea();
    
    /**
     * @brief Obtient le DPI de l'écran
     * @param hWnd Fenêtre pour laquelle obtenir le DPI
     * @return UINT DPI de l'écran
     */
    static UINT GetDPI(HWND hWnd = nullptr);
    
    /**
     * @brief Convertit des pixels en points DPI-aware
     * @param pixels Valeur en pixels
     * @param hWnd Fenêtre pour laquelle effectuer la conversion
     * @return int Valeur en points
     */
    static int PixelsToPoints(int pixels, HWND hWnd = nullptr);
    
    // ---------------------------------------------------------------------------
    // GDI Helper Functions
    // ---------------------------------------------------------------------------
    
    /**
     * @brief Crée une police avec des paramètres spécifiques
     * @param height Hauteur de la police
     * @param weight Épaisseur de la police
     * @param italic Italique
     * @param underline Souligné
     * @param faceName Nom de la police
     * @return HFONT Police créée, ou nullptr si échec
     */
    static HFONT CreateFont(int height, int weight = FW_NORMAL, bool italic = false, 
                           bool underline = false, const wchar_t* faceName = L"Segoe UI");
    
    /**
     * @brief Crée un pinceau de couleur solide
     * @param color Couleur RGB
     * @return HBRUSH Pinceau créé, ou nullptr si échec
     */
    static HBRUSH CreateSolidBrush(COLORREF color);
    
    /**
     * @brief Crée un stylo de couleur et d'épaisseur spécifiques
     * @param style Style du stylo
     * @param width Épaisseur du stylo
     * @param color Couleur RGB
     * @return HPEN Stylo créé, ou nullptr si échec
     */
    static HPEN CreatePen(int style, int width, COLORREF color);
    
    // ---------------------------------------------------------------------------
    // Color Utilities
    // ---------------------------------------------------------------------------
    
    /**
     * @brief Convertit une couleur RGB en COLORREF
     */
    static COLORREF RGB(byte r, byte g, byte b);
    
    /**
     * @brief Assombrit une couleur
     * @param color Couleur à assombrir
     * @param factor Facteur d'assombrissement (0.0 à 1.0)
     * @return COLORREF Couleur assombrie
     */
    static COLORREF DarkenColor(COLORREF color, float factor = 0.5f);
    
    /**
     * @brief Éclaircit une couleur
     * @param color Couleur à éclaircir
     * @param factor Facteur d'éclaircissement (0.0 à 1.0)
     * @return COLORREF Couleur éclaircie
     */
    static COLORREF LightenColor(COLORREF color, float factor = 0.5f);
    
    /**
     * @brief Mélange deux couleurs
     * @param color1 Première couleur
     * @param color2 Deuxième couleur
     * @param ratio Ratio de mélange (0.0 = color1, 1.0 = color2)
     * @return COLORREF Couleur mélangée
     */
    static COLORREF BlendColors(COLORREF color1, COLORREF color2, float ratio);
    
    // ---------------------------------------------------------------------------
    // String and Localization
    // ---------------------------------------------------------------------------
    
    /**
     * @brief Charge une chaîne de ressources
     * @param hInstance Instance de l'application
     * @param id ID de la chaîne
     * @return std::wstring Chaîne chargée, ou chaîne vide si erreur
     */
    static std::wstring LoadString(HINSTANCE hInstance, UINT id);
    
    /**
     * @brief Affiche une boîte de message
     * @param hWnd Fenêtre parente
     * @param text Texte du message
     * @param caption Titre du message
     * @param type Type de boîte de message (MB_OK, MB_ICONWARNING, etc.)
     * @return int Résultat de la boîte de message
     */
    static int MessageBox(HWND hWnd, const std::wstring& text, const std::wstring& caption, UINT type);
    
    // ---------------------------------------------------------------------------
    // System Information
    // ---------------------------------------------------------------------------
    
    /**
     * @brief Vérifie si le système est Windows 10 ou supérieur
     * @return bool vrai si Windows 10 ou supérieur
     */
    static bool IsWindows10OrGreater();
    
    /**
     * @brief Vérifie si le système est Windows 11 ou supérieur
     * @return bool vrai si Windows 11 ou supérieur
     */
    static bool IsWindows11OrGreater();
    
    /**
     * @brief Obtient la version du système d'exploitation
     * @return DWORD Version du système d'exploitation
     */
    static DWORD GetOSVersion();
};
