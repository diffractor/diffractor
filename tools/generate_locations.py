"""
Location data generator for Diffractor.

Downloads geonames data and generates location text files:
- location-countries.txt
- location-states.txt
- location-places.txt

Based on the original C# LocationImport utility.

Data sources:
- https://download.geonames.org/export/dump/
"""

import os
import sys
import zipfile
import urllib.request
from pathlib import Path
from dataclasses import dataclass, field
from typing import Optional


# ============================================================================
# Configuration
# ============================================================================

GEONAMES_BASE_URL = "https://download.geonames.org/export/dump/"

# Files to download from geonames
GEONAMES_FILES = {
    "allCountries": f"{GEONAMES_BASE_URL}allCountries.zip",
    "admin1CodesASCII": f"{GEONAMES_BASE_URL}admin1CodesASCII.txt",
    "countryInfo": f"{GEONAMES_BASE_URL}countryInfo.txt",
}

# URL for countries with alternate names (restcountries data converted to CSV)
COUNTRIES_EXTRA_URL = "https://raw.githubusercontent.com/mledoze/countries/master/dist/countries.csv"

# Minimum population for cities to include (except for allowed countries)
MIN_POPULATION = 1000

# Countries where we include cities regardless of population
ALLOWED_COUNTRIES = {"US", "GB", "DE", "AU", "NZ", "NL", "CA"}

# Maximum alternate names per record
MAX_ALT_NAMES = 16

# Console output interval
CONSOLE_OUTPUT_INTERVAL = 10000


# ============================================================================
# Data Classes
# ============================================================================

@dataclass
class StateRecord:
    """Represents a state/province/admin1 region."""
    code: str
    name: str
    
    def __str__(self) -> str:
        return f"{self.code}\t{self.name}"


@dataclass
class CountryRecord:
    """Represents a country."""
    code: str
    name: str
    alts: list[str] = field(default_factory=list)
    
    def __str__(self) -> str:
        parts = "\t".join(self.alts) if self.alts else ""
        if parts:
            return f"{self.code}\t{self.name}\t{parts}"
        return f"{self.code}\t{self.name}"


@dataclass
class CityRecord:
    """Represents a city/place."""
    id: int
    name: str
    latitude: float
    longitude: float
    state_code: str
    country_code: str
    population: int
    alternate_names: list[str] = field(default_factory=list)
    
    def __str__(self) -> str:
        alts = "\t" + "\t".join(self.alternate_names) if self.alternate_names else ""
        return f"{self.id}\t{self.latitude:.5g}\t{self.longitude:.5g}\t{self.state_code}\t{self.country_code}\t{self.population}\t{self.name}{alts}"


# ============================================================================
# Download Functions
# ============================================================================

def download_file(url: str, dest_path: Path, desc: str = "") -> None:
    """Download a file with progress indication."""
    if dest_path.exists():
        print(f"  {desc or dest_path.name} already exists, skipping download.")
        return
    
    print(f"  Downloading {desc or dest_path.name}...")
    
    def progress_hook(block_num, block_size, total_size):
        if total_size > 0:
            downloaded = block_num * block_size
            percent = min(100, downloaded * 100 // total_size)
            mb_downloaded = downloaded / (1024 * 1024)
            mb_total = total_size / (1024 * 1024)
            sys.stdout.write(f"\r    {percent}% ({mb_downloaded:.1f}/{mb_total:.1f} MB)")
            sys.stdout.flush()
    
    try:
        urllib.request.urlretrieve(url, dest_path, progress_hook)
        print()  # New line after progress
    except Exception as e:
        print(f"\n  Error downloading {url}: {e}")
        raise


def download_geonames_data(output_dir: Path) -> dict[str, Path]:
    """Download all required geonames data files."""
    print("Downloading geonames data...")
    
    output_dir.mkdir(parents=True, exist_ok=True)
    paths = {}
    
    for name, url in GEONAMES_FILES.items():
        if url.endswith(".zip"):
            zip_path = output_dir / f"{name}.zip"
            txt_path = output_dir / f"{name}.txt"
            
            download_file(url, zip_path, f"{name}.zip")
            
            # Extract if needed
            if not txt_path.exists():
                print(f"  Extracting {name}.txt...")
                with zipfile.ZipFile(zip_path, 'r') as zf:
                    zf.extract(f"{name}.txt", output_dir)
            
            paths[name] = txt_path
        else:
            txt_path = output_dir / f"{name}.txt"
            download_file(url, txt_path, f"{name}.txt")
            paths[name] = txt_path
    
    # Download countries extra data
    countries_extra_path = output_dir / "countries_extra.csv"
    download_file(COUNTRIES_EXTRA_URL, countries_extra_path, "countries_extra.csv")
    paths["countries_extra"] = countries_extra_path
    
    return paths


# ============================================================================
# States Processing
# ============================================================================

def load_states(states_file: Path) -> list[StateRecord]:
    """Load states from admin1CodesASCII.txt."""
    print(f"Loading states from {states_file.name}...")
    
    states = []
    records_loaded = 0
    
    with open(states_file, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            
            parts = line.split('\t')
            if len(parts) >= 2:
                code = parts[0]  # e.g., "US.CA"
                name = parts[1]
                
                states.append(StateRecord(code=code, name=name))
                records_loaded += 1
                
                if records_loaded % CONSOLE_OUTPUT_INTERVAL == 0:
                    print(f"\r  Loaded {records_loaded} states...", end='')
    
    print(f"\r  Loaded {records_loaded} states, completed.")
    return states


def process_states(states_file: Path, output_file: Path) -> None:
    """Process states and write output file."""
    print(f"Using states file: {states_file}")
    print(f"Producing states file: {output_file}")
    
    states = load_states(states_file)
    
    # Sort by code
    print("  Sorting states...")
    states.sort(key=lambda s: s.code)
    print("  Sorting states, completed.")
    
    # Write output
    records_written = 0
    with open(output_file, 'w', encoding='utf-8', newline='') as f:
        for state in states:
            f.write(str(state))
            f.write('\n')
            records_written += 1
            
            if records_written % CONSOLE_OUTPUT_INTERVAL == 0:
                print(f"\r  Wrote {records_written} states...", end='')
    
    print(f"\r  Wrote {records_written} states, completed.")


# ============================================================================
# Countries Processing
# ============================================================================

def load_countries(countries_file: Path, countries_extra_file: Path) -> list[CountryRecord]:
    """Load countries from countryInfo.txt and add alternate names from extra file."""
    print(f"Loading countries from {countries_file.name}...")
    
    countries = {}
    records_loaded = 0
    
    # Load main country info
    with open(countries_file, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            
            parts = line.split('\t')
            if len(parts) >= 5:
                code = parts[0]  # ISO code
                name = parts[4]  # Country name
                
                countries[code] = CountryRecord(code=code, name=name)
                records_loaded += 1
                
                if records_loaded % CONSOLE_OUTPUT_INTERVAL == 0:
                    print(f"\r  Loaded {records_loaded} countries...", end='')
    
    print(f"\r  Loaded {records_loaded} countries.")
    
    # Load alternate names from extra file
    print(f"  Loading alternate names from {countries_extra_file.name}...")
    max_alts = 0
    
    try:
        import csv
        with open(countries_extra_file, 'r', encoding='utf-8') as f:
            reader = csv.reader(f)
            header = next(reader)  # Get header row
            
            # Find column indices
            try:
                cca2_idx = header.index('cca2')
            except ValueError:
                # Try to find it
                cca2_idx = 3  # Default position
            
            try:
                alt_idx = header.index('altSpellings')
            except ValueError:
                alt_idx = 14  # Default position
            
            for parts in reader:
                if len(parts) <= max(cca2_idx, alt_idx):
                    continue
                
                code = parts[cca2_idx]
                alts_str = parts[alt_idx]
                
                if code in countries:
                    # Special handling for US
                    if code == "US":
                        alts_str = alts_str + ",United States,United States of America,America,the States,US,U.S.,USA,U.S.A."
                    
                    # Split alts and skip the first one (usually same as main name/ISO code)
                    alt_parts = [a.strip() for a in alts_str.split(',') if a.strip()]
                    if len(alt_parts) > 1:
                        alt_parts = alt_parts[1:]  # Skip first (usually the ISO code)
                    
                    # Remove duplicates while preserving order
                    seen = set()
                    unique_alts = []
                    for alt in alt_parts:
                        if alt.lower() not in seen:
                            seen.add(alt.lower())
                            unique_alts.append(alt)
                    
                    countries[code].alts = unique_alts
                    if len(unique_alts) > max_alts:
                        max_alts = len(unique_alts)
    
    except Exception as e:
        print(f"  Warning: Could not load alternate names: {e}")
        import traceback
        traceback.print_exc()
    
    print(f"  Maximum alternate names: {max_alts}")
    
    return list(countries.values())


def process_countries(countries_file: Path, countries_extra_file: Path, output_file: Path) -> None:
    """Process countries and write output file."""
    print(f"Using countries file: {countries_file}")
    print(f"Producing countries file: {output_file}")
    
    countries = load_countries(countries_file, countries_extra_file)
    
    # Sort by code
    print("  Sorting countries...")
    countries.sort(key=lambda c: c.code)
    print("  Sorting countries, completed.")
    
    # Write output
    records_written = 0
    with open(output_file, 'w', encoding='utf-8', newline='') as f:
        for country in countries:
            f.write(str(country))
            f.write('\n')
            records_written += 1
            
            if records_written % CONSOLE_OUTPUT_INTERVAL == 0:
                print(f"\r  Writing {records_written} countries...", end='')
    
    print(f"\r  Writing {records_written} countries, completed.")


# ============================================================================
# Cities Processing
# ============================================================================

def compact_alternate_names(actual_name: str, other_names: str) -> list[str]:
    """Compact alternate names, removing duplicates and limiting count."""
    if not other_names:
        return []
    
    candidates = other_names.split(',')
    results = []
    
    for name in candidates:
        name = name.strip()
        if not name:
            continue
        if name.lower() == actual_name.lower():
            continue
        if len(results) >= MAX_ALT_NAMES:
            break
        results.append(name)
    
    return results


def load_cities(cities_file: Path) -> list[CityRecord]:
    """Load cities from allCountries.txt (or cities1000.txt)."""
    print(f"Loading cities from {cities_file.name}...")
    print("  (This may take several minutes for allCountries.txt...)")
    
    cities = []
    records_loaded = 0
    max_alt_names = 0
    
    # Column indices for geonames allCountries.txt
    # geonameid, name, asciiname, alternatenames, latitude, longitude, 
    # feature class, feature code, country code, cc2, admin1 code, 
    # admin2 code, admin3 code, admin4 code, population, elevation, 
    # dem, timezone, modification date
    COL_ID = 0
    COL_NAME = 1
    COL_ALT_NAMES = 3
    COL_LATITUDE = 4
    COL_LONGITUDE = 5
    COL_FEATURE_CLASS = 6
    COL_COUNTRY_CODE = 8
    COL_ADMIN1_CODE = 10
    COL_POPULATION = 14
    
    with open(cities_file, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            
            parts = line.split('\t')
            if len(parts) < 15:
                continue
            
            try:
                feature_class = parts[COL_FEATURE_CLASS]
                country = parts[COL_COUNTRY_CODE]
                pop = int(parts[COL_POPULATION]) if parts[COL_POPULATION] else 0
                
                # Only include populated places (feature class P)
                if feature_class != 'P':
                    continue
                
                # Filter by population (include all from allowed countries, or pop > 1000)
                can_add = (pop > MIN_POPULATION) or (country in ALLOWED_COUNTRIES)
                if not can_add:
                    continue
                
                name = parts[COL_NAME]
                lname = name.lower()
                
                # Skip certain patterns
                if '(former)' in lname or 'diocese' in lname:
                    continue
                
                alt_names = compact_alternate_names(name, parts[COL_ALT_NAMES])
                
                if len(alt_names) > max_alt_names:
                    max_alt_names = len(alt_names)
                
                city = CityRecord(
                    id=int(parts[COL_ID]),
                    name=name,
                    latitude=float(parts[COL_LATITUDE]),
                    longitude=float(parts[COL_LONGITUDE]),
                    state_code=parts[COL_ADMIN1_CODE],
                    country_code=country,
                    population=pop,
                    alternate_names=alt_names
                )
                
                cities.append(city)
                records_loaded += 1
                
                if records_loaded % CONSOLE_OUTPUT_INTERVAL == 0:
                    print(f"\r  Loaded {records_loaded} cities...", end='')
            
            except (ValueError, IndexError) as e:
                continue  # Skip malformed lines
    
    print(f"\r  Loaded {records_loaded} cities, completed.")
    print(f"  Maximum alternate names: {max_alt_names}")
    
    return cities


def process_cities(cities_file: Path, output_file: Path) -> None:
    """Process cities and write output file."""
    print(f"Using cities file: {cities_file}")
    print(f"Producing cities file: {output_file}")
    
    cities = load_cities(cities_file)
    
    # Sort by name, state, country, lat, lon
    print("  Sorting cities...")
    cities.sort(key=lambda c: (c.name, c.state_code, c.country_code, c.latitude, c.longitude))
    print("  Sorting cities, completed.")
    
    # Write output
    records_written = 0
    with open(output_file, 'w', encoding='utf-8', newline='') as f:
        for city in cities:
            f.write(str(city))
            f.write('\n')
            records_written += 1
            
            if records_written % CONSOLE_OUTPUT_INTERVAL == 0:
                print(f"\r  Wrote {records_written} cities...", end='')
    
    print(f"\r  Wrote {records_written} cities, completed.")


# ============================================================================
# Main
# ============================================================================

def main():
    """Main entry point."""
    print("=" * 60)
    print("Diffractor Location Generator")
    print("=" * 60)
    print()
    
    # Determine directories - use project root structure
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    data_dir = project_root / "intermediate" / "geonames"
    output_dir = project_root / "exe"
    
    print(f"Project root: {project_root}")
    print(f"Download directory: {data_dir}")
    print(f"Output directory: {output_dir}")
    
    # Download data
    print()
    paths = download_geonames_data(data_dir)
    print()
    
    # Verify output directory exists
    if not output_dir.exists():
        print(f"Error: Output directory does not exist: {output_dir}")
        sys.exit(1)
    
    # Process states
    print()
    print("-" * 60)
    process_states(
        paths["admin1CodesASCII"],
        output_dir / "location-states.txt"
    )
    
    # Process countries
    print()
    print("-" * 60)
    process_countries(
        paths["countryInfo"],
        paths["countries_extra"],
        output_dir / "location-countries.txt"
    )
    
    # Process cities
    print()
    print("-" * 60)
    process_cities(
        paths["allCountries"],
        output_dir / "location-places.txt"
    )
    
    print()
    print("=" * 60)
    print(f"Done! Location files updated in: {output_dir}")
    print("  - location-countries.txt")
    print("  - location-states.txt")
    print("  - location-places.txt")
    print("=" * 60)


if __name__ == "__main__":
    main()
