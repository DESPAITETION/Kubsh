# kubsh - Custom Shell Implementation

A custom shell with VFS support for user information, developed as part of a university project.

## Features Checklist

✅ 1. Basic input/output with exit on Ctrl+D  
✅ 2. Input loop with Ctrl+D exit  
✅ 3. Exit command (`\q`)  
✅ 4. Command history with file persistence (`~/.kubsh_history`)  
✅ 5. Echo command (`echo "text"`)  
✅ 6. Command validation and error messages  
✅ 7. Environment variable management (`\e VAR` lists PATH entries)  
✅ 8. External command execution (searches in $PATH)  
✅ 9. SIGHUP signal handling ("Configuration reloaded")  
✅ 10. Disk information commands (`\l /dev/sda`)  
✅ 11. Virtual filesystem with auto user management (`~/users/`)

## Quick Start

### Local Development (Linux/WSL)

```bash
# Clone repository
git clone https://github.com/YOUR_USERNAME/kubsh.git
cd kubsh

# Build and run
make build
./kubsh
```
