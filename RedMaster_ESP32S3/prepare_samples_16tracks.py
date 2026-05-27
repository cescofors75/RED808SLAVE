#!/usr/bin/env python3
"""
prepare_samples_16tracks.py
Script para preparar samples con 16 tracks/instrumentos
- Limita a máximo 5-6 samples por instrumento
- Controla tamaño total (máximo 5-6 MB)
- Copia solo los samples necesarios a data/
"""

import os
import shutil
from pathlib import Path

# Configuración
MAX_SAMPLES_PER_INSTRUMENT = 6
MAX_TOTAL_SIZE_MB = 5.5
SOURCE_DIR = "samples_raw"  # Carpeta donde copias todos los samples
DATA_DIR = "data"

# 16 instrumentos para la drum machine
INSTRUMENTS = [
    "BD",    # Bass Drum (Bombo)
    "SD",    # Snare Drum (Caja)
    "CH",    # Closed Hi-Hat
    "OH",    # Open Hi-Hat
    "CP",    # Clap (Palmas)
    "CB",    # Cowbell (Cencerro)
    "RS",    # Rimshot (Aro)
    "CL",    # Claves
    "MA",    # Maracas
    "CY",    # Cymbal (Platillo)
    "TM1",   # Tom 1 (Agudo)
    "TM2",   # Tom 2 (Medio)
    "TM3",   # Tom 3 (Grave)
    "HC",    # Hand Clap
    "LC",    # Low Conga
    "PERC"   # Percussion (otros)
]

def get_wav_files(directory):
    """Obtiene todos los archivos .wav de un directorio"""
    return [f for f in Path(directory).glob("*.wav")]

def get_file_size_mb(filepath):
    """Retorna tamaño del archivo en MB"""
    return os.path.getsize(filepath) / (1024 * 1024)

def select_best_samples(files, max_count):
    """
    Selecciona los mejores samples según criterio:
    - Prioriza archivos más pequeños (menor latencia)
    - Mantiene variedad si hay diferentes nombres
    """
    # Ordenar por tamaño (más pequeños primero)
    sorted_files = sorted(files, key=lambda f: os.path.getsize(f))
    return sorted_files[:max_count]

def process_instrument(instrument_name):
    """Procesa un instrumento: filtra y copia samples"""
    source_path = Path(SOURCE_DIR) / instrument_name
    dest_path = Path(DATA_DIR) / instrument_name
    
    if not source_path.exists():
        print(f"⚠️  {instrument_name}: carpeta no encontrada en {source_path}")
        return 0, 0
    
    # Obtener todos los .wav
    wav_files = get_wav_files(source_path)
    
    if len(wav_files) == 0:
        print(f"⚠️  {instrument_name}: no hay archivos .wav")
        return 0, 0
    
    # Seleccionar los mejores
    selected = select_best_samples(wav_files, MAX_SAMPLES_PER_INSTRUMENT)
    
    # Crear carpeta destino
    dest_path.mkdir(parents=True, exist_ok=True)
    
    # Copiar archivos
    total_size = 0
    for i, src_file in enumerate(selected):
        dest_file = dest_path / f"{instrument_name}_{i+1:02d}.wav"
        shutil.copy2(src_file, dest_file)
        size_mb = get_file_size_mb(dest_file)
        total_size += size_mb
        print(f"  ✓ {dest_file.name} ({size_mb:.2f} MB)")
    
    print(f"✓ {instrument_name}: {len(selected)} samples ({total_size:.2f} MB)")
    return len(selected), total_size

def main():
    print("=" * 60)
    print("PREPARADOR DE SAMPLES - 16 TRACKS")
    print("=" * 60)
    print(f"Configuración:")
    print(f"  - Máximo samples por instrumento: {MAX_SAMPLES_PER_INSTRUMENT}")
    print(f"  - Límite total: {MAX_TOTAL_SIZE_MB} MB")
    print(f"  - Carpeta origen: {SOURCE_DIR}")
    print(f"  - Carpeta destino: {DATA_DIR}")
    print("=" * 60)
    print()
    
    # Verificar carpeta origen
    if not Path(SOURCE_DIR).exists():
        print(f"❌ ERROR: Carpeta {SOURCE_DIR} no existe")
        print(f"   Crea la carpeta y organiza tus samples en subcarpetas:")
        for inst in INSTRUMENTS:
            print(f"   - {SOURCE_DIR}/{inst}/")
        return
    
    # Procesar cada instrumento
    total_samples = 0
    total_size_mb = 0
    
    for instrument in INSTRUMENTS:
        count, size = process_instrument(instrument)
        total_samples += count
        total_size_mb += size
        print()
    
    # Resumen final
    print("=" * 60)
    print("RESUMEN")
    print("=" * 60)
    print(f"Total instrumentos procesados: {len(INSTRUMENTS)}")
    print(f"Total samples copiados: {total_samples}")
    print(f"Tamaño total: {total_size_mb:.2f} MB")
    
    if total_size_mb > MAX_TOTAL_SIZE_MB:
        print(f"⚠️  ADVERTENCIA: Tamaño excede el límite de {MAX_TOTAL_SIZE_MB} MB")
        print(f"   Considera reducir MAX_SAMPLES_PER_INSTRUMENT o comprimir samples")
    else:
        print(f"✓ Tamaño dentro del límite ({MAX_TOTAL_SIZE_MB - total_size_mb:.2f} MB disponibles)")
    
    print()
    print("Siguiente paso: pio run --target uploadfs")

if __name__ == "__main__":
    main()
