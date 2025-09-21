# OnBoarder — Troubleshooting / Dépannage

---

## English

### 1. No download works on Windows
**Symptom:**  
When trying to install an application, nothing happens, or the download fails immediately.

**Cause:**  
OnBoarder relies on **winget** (Windows Package Manager) to install and uninstall applications. If winget is not installed or not available in your system PATH, installations will not work.

**Solution:**  
1. Check if winget is installed:
   - Open a terminal (PowerShell or CMD).
   - Run:
     ```powershell
     winget --version
     ```
   - If you see a version number, winget is installed.
2. If not installed:
   - Update Windows 10/11 through **Microsoft Store** and make sure the *App Installer* package is installed.
   - Or download it directly from the Microsoft Store:  
     👉 [App Installer on Microsoft Store](https://apps.microsoft.com/store/detail/app-installer/9NBLGGH4NNS1)

---

## Français

### 1. Aucun téléchargement ne fonctionne sous Windows
**Symptôme :**  
Lorsqu’on essaie d’installer une application, rien ne se passe ou le téléchargement échoue immédiatement.

**Cause :**  
OnBoarder utilise **winget** (Windows Package Manager) pour installer et désinstaller des applications.  
Si winget n’est pas installé ou pas présent dans la variable PATH, les installations échoueront.

**Solution :**  
1. Vérifier si winget est installé :
   - Ouvre un terminal (PowerShell ou CMD).
   - Exécute :
     ```powershell
     winget --version
     ```
   - Si un numéro de version s’affiche, winget est bien installé.
2. Si non installé :
   - Mets à jour Windows 10/11 via le **Microsoft Store** et assure-toi que le package *App Installer* est installé.
   - Ou télécharge-le directement depuis le Microsoft Store :  
     👉 [App Installer sur Microsoft Store](https://apps.microsoft.com/store/detail/app-installer/9NBLGGH4NNS1)

---

ℹ️ More troubleshooting items will be added as issues are reported.  
ℹ️ D’autres problèmes connus seront ajoutés au fur et à mesure des retours.
