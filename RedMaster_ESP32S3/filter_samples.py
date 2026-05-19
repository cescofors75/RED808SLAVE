#!/usr/bin/env python3
"""
filter_samples.py
Filtra samples existentes en data/
- Máximo 6 samples por instrumento
- Mínimo 1 sample por instrumento
- Hace backup de los samples eliminados
"""

import os
import shutil
from pathlib import Path
from datetime import datetime

# Configuración
MAX_SAMPLES_PER_INSTRUMENT = 6
MIN_SAMPLES_PER_INSTRUMENT = 1
DATA_DIR = "data"
BACKUP_DIR = "samples_backup"

# 16 instrumentos
INSTRUMENTS = [
    "BD", "SD", "CH", "OH", "CP", "CB", "RS", "CL",
    "MA", "CY", "TM1", "TM2", "TM3", "HC", "LC", "PERC"
]

def get_wav_files(directory):
    """Obtiene todos los archivos .wav de un directorio"""
    path = Path(directory)
    if not path.exists():
        return []
    return sorted([f for f in path.glob("*.wav")])

def get_file_size_mb(filepath):
    """Retorna tamaño del archivo en MB"""
    return os.path.getsize(filepath) / (1024 * 1024)

def select_samples_to_keep(files, max_count):
    """
    Selecciona los samples a mantener:
    - Prioriza archivos más pequeños (mejor rendimiento)
    - Mantiene hasta max_count samples
    """
    # Ordenar por tamaño (más pequeños primero)
    sorted_files = sorted(files, key=lambda f: os.path.getsize(f))
    return sorted_files[:max_count]

def filter_instrument(instrument_name, backup_timestamp):
    """Filtra samples de un instrumento"""
    instrument_path = Path(DATA_DIR) / instrument_name
    
    if not instrument_path.exists():
        print(f"⚠️  {instrument_name}: carpeta no existe, creándola...")
        instrument_path.mkdir(parents=True, exist_ok=True)
        return 0, 0, 0
    
    # Obtener todos los .wav
    wav_files = get_wav_files(instrument_path)
    
    if len(wav_files) == 0:
        print(f"⚠️  {instrument_name}: SIN SAMPLES - añade al menos 1 archivo .wav")
        return 0, 0, 0
    
    num_original = len(wav_files)
    
    # Si ya están dentro del límite, no hacer nada
    if len(wav_files) <= MAX_SAMPLES_PER_INSTRUMENT:
        total_size = sum(get_file_size_mb(f) for f in wav_files)
        print(f"✓ {instrument_name}: {num_original} samples (OK, {total_size:.2f} MB)")
        return num_original, 0, total_size
    
    # Seleccionar los que se mantienen
    to_keep = select_samples_to_keep(wav_files, MAX_SAMPLES_PER_INSTRUMENT)
    to_remove = [f for f in wav_files if f not in to_keep]
    
    # Crear carpeta de backup
    backup_path = Path(BACKUP_DIR) / backup_timestamp / instrument_name
    backup_path.mkdir(parents=True, exist_ok=True)
    
    # Mover archivos excedentes a backup
    removed_size = 0
    for file in to_remove:
        backup_file = backup_path / file.name
        shutil.move(str(file), str(backup_file))
        removed_size += get_file_size_mb(backup_file)
    
    kept_size = sum(get_file_size_mb(f) for f in to_keep)
    
    print(f"✓ {instrument_name}: {len(to_keep)} samples mantenidos, {len(to_remove)} movidos a backup")
    print(f"  Tamaño: {kept_size:.2f} MB (liberados {removed_size:.2f} MB)")
    
    return len(to_keep), len(to_remove), kept_size

def main():
    print("=" * 70)
    print("FILTRADOR DE SAMPLES - 16 TRACKS")
    print("=" * 70)
    print(f"Configuración:")
    print(f"  - Máximo samples por instrumento: {MAX_SAMPLES_PER_INSTRUMENT}")
    print(f"  - Mínimo samples por instrumento: {MIN_SAMPLES_PER_INSTRUMENT}")
    print(f"  - Carpeta de trabajo: {DATA_DIR}")
    print(f"  - Carpeta de backup: {BACKUP_DIR}")
    print("=" * 70)
    print()
    
    # Verificar carpeta data
    if not Path(DATA_DIR).exists():
        print(f"❌ ERROR: Carpeta {DATA_DIR} no existe")
        return
    
    # Timestamp para el backup
    backup_timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    
    # Procesar cada instrumento
    total_kept = 0
    total_removed = 0
    total_size_mb = 0
    missing_instruments = []
    
    for instrument in INSTRUMENTS:
        kept, removed, size = filter_instrument(instrument, backup_timestamp)
        total_kept += kept
        total_removed += removed
        total_size_mb += size
        
        if kept == 0:
            missing_instruments.append(instrument)
        
        print()
    
    # Resumen final
    print("=" * 70)
    print("RESUMEN")
    print("=" * 70)
    print(f"Instrumentos procesados: {len(INSTRUMENTS)}")
    print(f"Samples mantenidos: {total_kept}")
    print(f"Samples movidos a backup: {total_removed}")
    print(f"Tamaño total final: {total_size_mb:.2f} MB")
    
    if missing_instruments:
        print(f"\n⚠️  ADVERTENCIA: Los siguientes instrumentos NO tienen samples:")
        for inst in missing_instruments:
            print(f"   - {inst}")
        print(f"\n   Añade al menos 1 sample .wav a cada carpeta en {DATA_DIR}/")
    else:
        print(f"\n✓ Todos los instrumentos tienen samples")
    
    if total_removed > 0:
        print(f"\n✓ Backup guardado en: {BACKUP_DIR}/{backup_timestamp}/")
        print(f"  (Puedes eliminar esta carpeta si no necesitas los samples)")
    
    print("\nSiguiente paso: pio run --target uploadfs")

if __name__ == "__main__":
    main()
