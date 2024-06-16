
PRAGMA page_size = 2048;
PRAGMA journal_mode = WAL;
PRAGMA synchronous = FULL; 

CREATE TABLE IF NOT EXISTS item_properties (
	folder TEXT NOT NULL,
	name TEXT NOT NULL,
	properties BLOB NULL,	
	hash BLOB NULL,	
	media_position INTEGER,
	flag INTEGER,
	crc INTEGER,
	last_scanned INTEGER64,	
	last_indexed INTEGER64,	
	
	CONSTRAINT pk_item_properties PRIMARY KEY (folder, name) 
);

CREATE TABLE IF NOT EXISTS item_thumbnails (
	folder TEXT NOT NULL,
	name TEXT NOT NULL,
	bitmap BLOB NULL,
	cover_art BLOB NULL,
	last_scanned INTEGER64,	
	
	CONSTRAINT pk_item_thumbnails PRIMARY KEY (folder, name) 
);

CREATE TABLE IF NOT EXISTS web_service_cache (
	key TEXT NOT NULL PRIMARY KEY,
	created_date INTEGER64 NOT NULL,
	value TEXT	
);

CREATE TABLE IF NOT EXISTS item_imports (
	name TEXT NOT NULL,
	modified INTEGER64 NOT NULL,
	size INTEGER64  NOT NULL,
	imported INTEGER64 NOT NULL,	
	
	CONSTRAINT pk_item_imports PRIMARY KEY (name, modified, size) 
);
