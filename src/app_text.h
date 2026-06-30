// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Localized text strings and translations. Contains all user-visible text for
// internationalization support with PO file loading and plural text formatting.

#pragma once

std::string language_name(std::string_view code);


using text_mapping = df::hash_map<std::string_view, std::reference_wrapper<text_t>>;

struct plural_text
{
	plural_text(const std::string_view o, const std::string_view p) : one(o), plural(p)
	{
	}

	text_t one;
	text_t plural;
};

struct po_entry
{
	std::string id;
	std::string str;
	std::string id_plural;
	std::string str_plural;

	bool is_empty() const
	{
		return id.empty() &&
			str_plural.empty() &&
			str.empty() &&
			id_plural.empty();
	}

	void clear()
	{
		id.clear();
		str_plural.clear();
		str.clear();
		id_plural.clear();
	}
};

std::vector<po_entry> load_po(df::file_path lang_file);

std::string format_plural_text(const plural_text& fmt, int64_t count, int64_t of_total = 0);
std::string format_plural_text(const plural_text& fmt, std::string_view first_name, int64_t count,
                               df::file_size size, int64_t of_total = 0);
std::string format_plural_text(const plural_text& fmt, const df::item_set& items);
std::string format_plural_text(const plural_text& fmt, const std::vector<std::string>& result);

std::string_view tt_prep(std::string_view);

struct app_text_t
{
	text_mapping _text_mapping;
	std::vector<std::reference_wrapper<plural_text>> _all_plurals;
	std::vector<std::reference_wrapper<text_t>> _all_texts;

	app_text_t();

	app_text_t(const app_text_t& other1) = delete;
	app_text_t(app_text_t&& other1) noexcept = delete;
	app_text_t& operator=(const app_text_t& other1) = delete;
	app_text_t& operator=(app_text_t&& other1) noexcept = delete;

	void load_lang(std::string_view lang_file, const std::vector<po_entry>& entries);
	std::vector<po_entry> gen_po() const;

	void clear() const;
	void calc_text_mapping();

	std::string translate_text(const std::string& text, std::string_view scope = {}) const;
	std::vector<std::string> add_translate_text(const std::vector<str::cached>& text,
	                                            std::string_view scope = {}) const;

	text_t nav_folders_title = "Folders";
	text_t nav_drives_title = "Drives";
	text_t nav_search_title = "Search";
	text_t nav_history_title = "History";
	text_t nav_tags_title = "Favorite Tags";
	text_t nav_countries_title = "Countries";
	text_t error_invalid_path_fmt = "The file name you have chosen '{}' is invalid.";
	text_t error_invalid_path = "File names cannot contain the following characters:";
	text_t error_save_failed = "Diffractor could not save the updated image file.";
	text_t error_create_file_failed_fmt = "The destination file '{}' cannot be opened or created.";
	text_t error_rename_failed = "Failed to rename.";
	text_t error_ole_failed = "OleInitialize Failed";
	text_t error_windows_common_controls_failed = "Failed to register windows common control classes.";
	text_t error_create_folder_failed_fmt = "The destination folder '{}' cannot be opened or created.";
	text_t error_mapi_failed =
		"Failed to send EMail using MAPI. Your Email client may not be configured in a compatible way with Diffractor.";
	text_t error_atl_failed = "Failed to Initialize ATL";
	text_t error_atl_direct3d = "Failed to Initialize Direct3d";
	text_t error_create_window_failed = "Diffractor cannot be started. The main window could not be created.";
	text_t error_winsock_failed = "Failed to initialize Winsock";
	text_t error_sse2_needed = "Diffractor requires a processor with sse2.";
	text_t error_unsupported_os = "Diffractor requires Windows 7 or newer.";
	text_t error_index_database_failed = "Index database failed.";
	text_t error_cannot_continue = "Diffractor has a serious problem and cannot continue.";
	text_t command_menu_group_sort = "Group or Sort";
	text_t command_view_sort = "Sort";
	text_t command_view_menu = "Menu";
	text_t tooltip_view_menu = "More options and controls";
	text_t command_rotate_reset = "Reset rotation";
	text_t tooltip_rotate_reset = "Reset straighten and rotate";
	text_t command_color_reset = "Reset color changes";
	text_t tooltip_color_reset = "Reset color adjustments";
	text_t command_view_select = "Select Items";
	text_t command_view_rate_label = "Rate or Label";
	text_t command_navigate = "Navigate";
	text_t command_display_options = "Display options";
	text_t command_view_help = "About Diffractor";
	text_t command_browse_parent = "Parent";
	text_t command_browse_back = "Back";
	text_t command_browse_forward = "Forward";
	text_t command_new_folder = "New Folder";
	text_t command_rename = "Rename";
	text_t command_rename_files = "Rename files";
	text_t command_locate = "Add location";
	text_t command_print = "Print";
	text_t command_nav_bar = "Show sidebar";
	text_t tooltip_nav_bar = "Contains common folders, searches and favorites";
	text_t command_delete = "Delete";
	text_t command_revert = "Revert to original";
	text_t command_select_invert = "Invert selection";
	text_t command_copy = "Copy to folder";
	text_t command_move = "Move to folder";
	text_t command_file_properties = "Open properties";
	text_t command_share_email = "Email";
	text_t command_desktop_background = "Desktop background";
	text_t command_select_nothing = "Select none";
	text_t command_download = "Download";
	text_t command_options = "General options";
	text_t command_customise = "Customise sidebar";
	text_t command_collection_options = "Collection options";
	text_t command_view_items = "Show items";
	text_t command_flatten = "Show items in subfolders";
	text_t command_refresh = "Refresh";
	text_t command_fullscreen = "Fullscreen";
	text_t tooltip_fullscreen = "Toggle full screen for media-items or open non-media-items.";
	text_t command_show_thumbnails = "Hide/Show thumbnails";
	text_t command_autoplay = "Autoplay selected media";
	text_t command_last_played_pos = "Resume playing media from last position";
	text_t command_repeat_toggle = "Toggle repeat play";
	text_t command_playback_menu = "Playback Options";
	text_t command_playback_toolbar = "Playback";
	text_t command_repeat_one = "Repeat play one item";
	text_t command_repeat_all = "Repeat play all media items";
	text_t command_repeat_none = "Don't repeat";
	text_t command_scale_up = "Scale up items";
	text_t tooltip_scale_up =
		"Scale up small photos and videos. Large media is automatically scaled down to fit on the screen.";
	text_t command_select_all = "Select all";
	text_t command_related = "Duplicate or related items in the collection";
	text_t command_pin = "Pin or hold item";
	text_t tooltip_pin =
		"Pinned items stay selected. Use this when you want to quickly compare an item to other items.";
	text_t command_toggle_item_size = "Toggle thumbnail size";
	text_t command_convert_or_resize = "Convert or Resize";
	text_t command_album_artist = "Update artist and album";
	text_t command_edit_metadata = "Edit metadata";
	text_t command_copyright = "Update copyright";
	text_t command_burn = "Burn to disk";
	text_t command_keyboard = "Keyboard reference";
	text_t command_open_with = "Open with";
	text_t command_open = "Open";
	text_t tooltip_open = "Open selected items with external app";
	text_t command_language = "Language";
	text_t tooltip_tag_with = "Tag or un-tag selected items";
	text_t tooltip_language = "Change language";
	text_t command_tools = "Tools";
	text_t tooltip_tools = "Process selected items";
	text_t command_eject = "Eject";
	text_t command_minimize = "Minimize";
	text_t command_maximize = "Maximize";
	text_t command_restore = "Restore";
	text_t command_close = "Close";
	text_t command_import = "Import";
	text_t command_browse_previous_group = "Previous item group";
	text_t command_browse_next_group = "Next item group";
	text_t command_browse_previous_folder = "Previous folder or search";
	text_t command_browse_next_folder = "Next folder or search";
	text_t command_browse_previous_item = "Select previous item";
	text_t command_browse_previous_item_extend = "Extend selection to previous item";
	text_t command_browse_next_item = "Select next item";
	text_t command_browse_next_item_extend = "Extend selection to next item";
	text_t command_play = "Play/Pause";
	text_t tooltip_play = "Play or pause video and audio files. Start a slide show if a photo is selected.";
	text_t command_zoom = "Zoom 100%";
	text_t command_toggle_volume = "Toggle volume";
	text_t command_volume200 = "Volume 200%";
	text_t command_volume100 = "Volume 100%";
	text_t command_volume75 = "Volume 75%";
	text_t command_volume50 = "Volume 50%";
	text_t command_volume25 = "Volume 25%";
	text_t command_volume0 = "Mute";
	text_t command_rotate_anticlockwise = "Rotate anticlockwise";
	text_t command_rotate_clockwise = "Rotate clockwise";
	text_t command_show_in_folder = "Open containing folder";
	text_t command_show_in_file_browser = "Show in file browser";
	text_t command_file_search = "Search";
	text_t command_toggle_details = "Toggle thumbnails or details";
	text_t tooltip_toggle_details_selected = "Toggle between thumbnails or details for selected item group";
	text_t tooltip_toggle_details_all = "Toggle between thumbnails or details view";
	text_t command_capture = "Save current video frame";
	text_t command_edit = "Edit";
	text_t tooltip_edit1 = "Color, straighten or crop photos.";
	text_t tooltip_edit2 = "Add and edit metadata.";
	text_t command_edit_copy = "Copy";
	text_t command_edit_cut = "Cut";
	text_t command_edit_paste = "Paste";
	text_t command_app_exit = "Exit";
	text_t command_rate_1 = "Rate as 1 star";
	text_t command_rate_2 = "Rate as 2 stars";
	text_t command_rate_3 = "Rate as 3 stars";
	text_t command_rate_4 = "Rate as 4 stars";
	text_t command_rate_5 = "Rate as 5 stars";
	text_t command_rate_0 = "Remove rating";
	text_t command_rate_rejected = "Reject";
	text_t command_label_approved = "Approved";
	text_t command_label_to_do = "To Do";
	text_t command_label_select = "Select";
	text_t command_label_review = "Review";
	text_t command_label_second = "Second";
	text_t command_label_none = "No Label";
	text_t command_group_shuffle = "Show items in random order";
	text_t command_toggle_group_by = "Toggle item grouping";
	text_t command_group_file_type = "Group by File type";
	text_t command_group_size = "Group by Size";
	text_t command_group_extension = "Group by Extension";
	text_t command_group_location = "Group by Location";
	text_t command_group_rating = "Group by Rating/Label";
	text_t command_group_created = "Group by Date Created";
	text_t command_group_modified = "Group by Date Modified";
	text_t command_group_resolution = "Group by Resolution";
	text_t command_group_camera = "Group by Camera";
	text_t command_group_album = "Group by Album/Show";
	text_t command_group_presence = "Group by Collection presence";
	text_t command_group_folder = "Group by Folder";
	text_t command_sort_dates_descending = "Sort dates descending";
	text_t command_sort_dates_ascending = "Sort dates ascending";
	text_t command_sort_name = "Sort by Name";
	text_t command_sort_size = "Sort by Size";
	text_t command_sort_def = "Sort by Default";
	text_t command_sort_date_modified = "Sort by Date modified";
	text_t command_highlight_large_items = "Highlight large items in yellow";
	text_t command_open_google_map = "Open in google maps";
	text_t tooltip_flag_for_delete = "Items can be flagged for later delete.";
	text_t command_adjust_date = "Adjust date and time";
	text_t command_view_large_font = "Large font";
	text_t command_scan = "Scan";
	text_t command_save = "Save and close";
	text_t command_save_as = "Save as";
	text_t command_save_and_back = "Save and open previous";
	text_t command_save_and_next = "Save and open next";
	text_t command_save_options = "Change save options";
	text_t command_new_version = "New version available";
	text_t command_check_for_updates = "Check for updates";
	text_t searching_text = "Searching...";
	text_t no_results = "No results";
	text_t folder_onedrive = "onedrive";
	text_t folder_picture = "picture";
	text_t folder_video = "videos";
	text_t folder_music = "music";
	text_t disk_label = "Label";
	text_t disk_capacity = "Capacity";
	text_t disk_free = "Free";
	text_t disk_used = "Used";
	text_t disk_system = "System";
	text_t duplicates = "Duplicates";
	text_t duplicates_tooltip = "Duplicate or similar items in the collection.";
	text_t unknown = "Unknown";
	text_t sort_by_presence = "Presence";
	text_t sort_by_file_type = "Type";
	text_t sort_by_shuffle = "Show items in random order";
	text_t sort_by_name = "Name";
	text_t sort_by_size = "Size";
	text_t sort_by_def = "Default";
	text_t sort_by_extension = "Extension";
	text_t sort_by_location = "Location";
	text_t sort_by_album_show = "Album-Show";
	text_t sort_by_rating_label = "Rating-Label";
	text_t sort_by_resolution = "Resolution";
	text_t sort_by_Folder = "Folder";
	text_t no_items_are_selected = "No items are selected";
	text_t not_supported_cloud = "Cloud items are not supported.";
	text_t not_supported_readonly = "Readonly items are not supported.";
	text_t not_supported_readonly_metadata = "Cannot updated metadata of readonly items.";
	text_t not_supported_photo = "Items must be a photo or image.";
	text_t not_supported_save_format = "Diffractor can only save or update JPG, WEBP and PNG image files.";
	text_t cannot_edit = "Cannot updated metadata of this file type.";
	text_t not_supported_folder = "Folders are not supported.";
	text_t rating_remove_fmt = "Click {} to remove rating";
	text_t rating_keys = "Number keys 0-5";
	text_t show_related = "press R to view duplicate or related items in the collection";
	text_t item_oriented = "Show items rotated";
	text_t show_verbose_metadata = "Show verbose metadata";
	text_t hide_verbose_metadata = "Hide verbose metadata";
	text_t item_oriented_tooltip_fmt = "This item is shown oriented based on camera information. [{}]";
	text_t item_show_oriented = "Click to toggle if all items are shown rotated.";
	text_t num_of = "of";
	text_t before = "Before";
	text_t after = "After";
	text_t search_or_folder = "Search criteria or folder";
	text_t button_change = "Change...";
	text_t button_ok = "&OK";
	text_t button_cancel = "&Cancel";
	text_t button_close = "&Close";
	text_t version = "Version";
	text_t orientation_top_left = "top-left";
	text_t orientation_top_right = "top-right";
	text_t orientation_bottom_right = "bottom-right";
	text_t orientation_bottom_left = "bottom-left";
	text_t orientation_left_top = "left-top";
	text_t orientation_right_top = "right-top";
	text_t orientation_right_bottom = "right-bottom";
	text_t orientation_left_bottom = "left-bottom";
	text_t default_favorite_tags =
		"family food friends landscape nature night portrait selfie todo travel urban viewed";
	text_t default_write_name = "results";
	text_t authorized = "Diffractor has been authorized";
	text_t click_to_search_similar = "Click to search for similar items in the collection.";
	text_t title_rate = "rate";
	text_t title_folder = "folder:{}";
	text_t title_error = "Diffractor ERROR";
	text_t type_to_search = "Type to search";
	text_t prop_name_album = "Album";
	text_t prop_name_show = "Show";
	text_t prop_name_season = "Season";
	text_t prop_name_episode = "Episode";
	text_t prop_name_artist = "Artist";
	text_t prop_name_albumartist = "Album artist";
	text_t prop_name_audiocodec = "Audio codec";
	text_t prop_name_bitrate = "Bitrate";
	text_t prop_name_cameramanufacturer = "Camera manufacturer";
	text_t prop_name_camera = "Camera";
	text_t prop_name_channels = "Channels";
	text_t prop_name_samplerate = "Sample rate";
	text_t prop_name_sampletype = "Sample type";
	text_t prop_name_place = "Place";
	text_t prop_name_comment = "Comment";
	text_t prop_name_description = "Description";
	text_t prop_name_composer = "Composer";
	text_t prop_name_copyrightcredit = "Copyright credit";
	text_t prop_name_copyrightsource = "Copyright source";
	text_t prop_name_copyrightcreator = "Copyright creator";
	text_t prop_name_copyrightnotice = "Copyright notice";
	text_t prop_name_copyrighturl = "Copyright URL";
	text_t prop_name_country = "Country//property";
	text_t prop_name_createdexif = "Created exif";
	text_t prop_name_digitized = "Digitized";
	text_t prop_name_created = "Created";
	text_t prop_name_disk = "Disk";
	text_t prop_name_dimensions = "Dimensions";
	text_t prop_name_duration = "Duration";
	text_t prop_name_encoder = "Encoder";
	text_t prop_name_encodingtool = "Encoding tool";
	text_t prop_name_exposure = "Exposure";
	text_t prop_name_fnumber = "FNumber";
	text_t prop_name_focallength = "Focal length";
	text_t prop_name_35mmequivalent = "Focal length 35mm equivalent";
	text_t prop_name_pixelformat = "Pixel format";
	text_t prop_name_genre = "Genre";
	text_t prop_name_iso = "ISO";
	text_t prop_name_latitude = "Latitude";
	text_t prop_name_lens = "Lens";
	text_t prop_name_longitude = "Longitude";
	text_t prop_name_mediacategory = "Media category";
	text_t prop_name_megapixels = "Megapixels";
	text_t prop_name_modified = "Modified";
	text_t prop_name_null = "Null";
	text_t prop_name_orientation = "Orientation";
	text_t prop_name_publisher = "Publisher";
	text_t prop_name_performer = "Performer";
	text_t prop_name_rating = "Rating";
	text_t prop_name_size = "Size";
	text_t prop_name_state = "State";
	text_t prop_name_streams = "Streams";
	text_t prop_name_synopsis = "Synopsis";
	text_t prop_name_tag = "Tag";
	text_t prop_name_title = "Title";
	text_t prop_name_track = "Track";
	text_t prop_name_videocodec = "Video codec";
	text_t prop_name_year = "Year";
	text_t prop_name_id = "Id";
	text_t prop_name_filename = "Filename";
	text_t prop_name_rawfile = "Raw file";
	text_t prop_name_system = "System";
	text_t prop_name_game = "Game//property";
	text_t prop_name_label = "Label";
	text_t prop_name_doc_id = "Document Id";
	text_t menu_add_fmt = "Add '{}'";
	text_t menu_undo = "Undo";
	text_t menu_cut = "Cut";
	text_t menu_copy = "Copy";
	text_t menu_move = "Move";
	text_t menu_paste = "Paste";
	text_t menu_delete = "Delete";
	text_t menu_select_all = "Select All";
	text_t error_same_file = "The source and destination files are the same file.";
	text_t error_many_src_1_dest =
		"Multiple file paths were specified in the source buffer, but only one destination file path.";
	text_t error_diff_dir =
		"Rename operation was specified but the destination path is a different directory. Use the move operation instead.";
	text_t error_src_root_dir = "The source is a root directory, which cannot be moved or renamed.";
	text_t error_op_cancelled =
		"The operation was canceled by the user, or silently canceled if the appropriate flags were supplied to SHFileOperation.";
	text_t error_dest_subtree = "The destination is a subtree of the source.";
	text_t error_access_denied_src = "Security options denied access to the source.";
	text_t error_path_too_deep = "The source or destination path exceeded or would exceed MAX_PATH.";
	text_t error_many_dest =
		"The operation involved multiple destination paths, which can fail in the case of a move operation.";
	text_t error_invalid_files = "The path in the source or destination or both was invalid.";
	text_t error_dest_same_tree = "The source and destination have the same parent folder.";
	text_t error_fld_dest_is_file = "The destination path is an existing file.";
	text_t error_file_dest_is_fld = "The destination path is an existing folder.";
	text_t error_filename_too_long = "The name of the file exceeds MAX_PATH.";
	text_t error_dest_is_cd_rom = "The destination is a read-only CD-ROM, possibly unformatted.";
	text_t error_dest_is_dvd = "The destination is a read-only DVD, possibly unformatted.";
	text_t error_dest_is_cd_record = "The destination is a writable CD-ROM, possibly unformatted.";
	text_t error_file_too_large =
		"The file involved in the operation is too large for the destination media or file system.";
	text_t error_src_is_cdrom = "The source is a read-only CD-ROM, possibly unformatted.";
	text_t error_src_is_dvd = "The source is a read-only DVD, possibly unformatted.";
	text_t error_src_is_cd_record = "The source is a writable CD-ROM, possibly unformatted.";
	text_t error_max = "MAX_PATH was exceeded during the operation.";
	text_t error_unknown = "An unknown error occurred.";
	text_t error_on_dest = "An unspecified error occurred on the destination.";
	text_t error_dst_root_dir = "Destination is a root directory and cannot be renamed.";
	text_t select_folder = "Select Folder...";
	text_t jpeg_best = "JPEG - Best compression for photos";
	text_t png_best = "PNG - Best for non photos";
	text_t webp_best = "WEBP - Best for the web";
	text_t error_connect_scanner = "Could not connect to scanner";
	text_t error_scanner = "Scanner reported an error";
	text_t error_save_image = "Failed to save image.";
	text_t indexing = "Indexing";
	text_t detecting_folders = "Detecting {} folders";
	text_t indexing_message =
		"Diffractor indexes your photos and videos to enable searching and duplicate detection.";
	text_t collection_options_help = "You can choose what is indexed in collection options (press Ctrl+F6).";
	text_t index_later = "Index later";
	text_t collection_options = "Collection options";
	text_t empty = "Empty";
	text_t loading = "Loading...";
	text_t nothing_found1 = "Nothing found.";
	text_t nothing_found2 =
		"Diffractor only searches in folders that are indexed. Use collection options to add more folders. (Ctrl+F6)";
	text_t button_save = "&Save";
	text_t button_dont_save = "&Don't Save";
	text_t save_changes = "Save Changes";
	text_t save_as_jpeg_fmt = "Diffractor cannot crop, rotate or color {} files. You can save the file as a JPEG?";
	text_t button_save_as_jpeg = "As &JPEG";
	text_t changes = "Changes";
	text_t has_changes = "{} has modifications";
	text_t saving_file_name = "Saving {}...";
	text_t options_backup_copy = "When overwriting a photo with irreversible changes make an original backup copy";
	text_t options_jpeg_quality = "JPEG save quality";
	text_t options_webp_quality = "WEBP save quality";
	text_t help_tag1 = "Separate each tag with a space: Australia holiday camping.";
	text_t help_tag2 = "Join 2 words together in one tag using quotes: \"national park\".";
	text_t help_artist = "Separate each artist with a comma ',' or slash '/'";
	text_t straighten = "Straighten";
	text_t color = "Color";
	text_t vibrance = "Vibrance";
	text_t darks = "Darks";
	text_t midtones = "Midtones";
	text_t lights = "Lights";
	text_t contrast = "Contrast";
	text_t brightness = "Brightness";
	text_t saturation = "Saturation";
	text_t metadata = "Metadata";
	text_t staring = "Staring";
	text_t tags_title = "Tags";
	text_t album_artist = "Album Artist";
	text_t copyright_title = "Copyright";
	text_t month_january = "January";
	text_t month_february = "February";
	text_t month_march = "March";
	text_t month_april = "April";
	text_t month_may = "May";
	text_t month_june = "June";
	text_t month_july = "July";
	text_t month_august = "August";
	text_t month_september = "September";
	text_t month_october = "October";
	text_t month_november = "November";
	text_t month_december = "December";
	text_t month_short_jan = "jan";
	text_t month_short_feb = "feb";
	text_t month_short_mar = "mar";
	text_t month_short_apr = "apr";
	text_t month_short_may = "may";
	text_t month_short_jun = "jun";
	text_t month_short_jul = "jul";
	text_t month_short_aug = "aug";
	text_t month_short_sep = "sep";
	text_t month_short_oct = "oct";
	text_t month_short_nov = "nov";
	text_t month_short_dec = "dec";
	text_t text_true = "true";
	text_t text_false = "false";
	text_t compare = "Compare";
	text_t compare_tooltip = "Hold down left mouse button to compare items.";
	text_t zoom_tooltip = "Hold down left mouse button to show photo 100% size.";
	text_t zoom_kb = "press ctrl+space";
	text_t query_or = "or";
	text_t query_and = "and";
	text_t query_with = "with";
	text_t query_without = "without";
	text_t query_created = "created";
	text_t query_modified = "modified";
	text_t query_age = "age";
	text_t query_related = "related";
	text_t query_duplicates = "duplicates";
	text_t query_duplicates_alt1 = "dups";
	text_t query_duplicates_alt2 = "duplicate";
	text_t pixels_title = "Pixels";
	text_t pixels_icon = "icon";
	text_t pixels_small = "small";
	text_t resolution_none = "No resolution";
	text_t group_title_no_extension = "No extension";
	text_t group_title_no_location = "No location";
	text_t group_title_no_rating = "No rating";
	text_t group_title_no_resolution = "No resolution";
	text_t group_title_no_camera = "No camera";
	text_t group_title_no_album_or_show = "No album or show";
	text_t group_title_no_value = "No value";
	text_t group_title_shuffle = "random";
	text_t group_title_size_range_fmt = "{} to {}";
	text_t group_title_today = "Today";
	text_t group_title_yesterday = "Yesterday";
	text_t group_title_items = "Items";
	text_t cancel_was_pressed_after = "Cancel was pressed after ";
	text_t rate_title = "Rate selected items";
	text_t open_in_browser_title = "Open in file browser";
	text_t new_folder_name = "New Folder";
	text_t new_folder_title = "New Folder";
	text_t burn_title = "Burn";
	text_t print_title = "Print";
	text_t remove_metadata_title = "Remove Metadata";
	text_t folder_noun = "folder";
	text_t folder_noun_plural = "folders";
	text_t item_noun = "item";
	text_t item_noun_plural = "items";
	text_t rename_help_template_1 = "When specifying a template:";
	text_t rename_help_template_2 = "Use '#' to specify position of the numeric sequence";
	text_t rename_help_template_3 =
		"Use metadata templates '{property-name}' to include file properties in the name.";
	text_t for_example = "For example:";
	text_t rename_help_template_example_2 = "photo-###";
	text_t rename_help_template_example_3 = "{year}-{month}-###";
	text_t rename_help_template_example_4 = "travel-{country}-###";

	text_t rename_info = "Rename multiple files based on an incrementing sequence or file metadata template.";
	text_t rename_template_label = "Template:";
	text_t rename_template_start_label = "Start at:";
	text_t rename_label = "New name:";
	text_t open_properties_title = "Open Properties";
	text_t pasted_file_name = "pasted";
	text_t resized_max_label = "Maximum photo dimension (in pixels):";
	text_t dest_folder_label = "Destination folder:";
	text_t open_dest = "Open destination folder afterwards";
	text_t failed_to_create_folder_fmt = "Failed to create folder: {}";
	text_t button_rotate = "&Rotate";
	text_t desktop_background_info = "Set the desktop background to the current image.";
	text_t maximize_image = "Maximize image";
	text_t button_background = "&Background";
	text_t button_delete = "&Delete";
	text_t delete_error = "Failed to Delete Selected File.";
	text_t button_update = "&Update";
	text_t copyright_notice = "Notice";
	text_t copyright_creator = "Creator";
	text_t copyright_source = "Source";
	text_t copyright_credit = "Credit";
	text_t copyright_url = "URL";
	text_t location_overwrite_gps = "Overwrite GPS coordinates when setting a location.";
	text_t location_not_selected = "No location selected.";
	text_t location_not_loaded = "Location could not be loaded from database or internet.";
	text_t destination_folder = "Destination folder:";
	text_t lossless_compression = "Lossless compression";
	text_t limit_output_dimensions = "Limit output to a maximum dimension (in pixels).";
	text_t button_convert = "&Convert";
	text_t dest_name_label = "Destination name:";
	text_t tag_add_remove = "Add or remove Tags";
	text_t help_tag_add_remove = "Tags can be removed by prefixing with a minus: -remove";
	text_t tag_add_or_remove_label = "Tags to add or remove:";
	text_t tags_favorite_label = "Favorite Tags";
	text_t tags_common_label = "Common Tags";
	text_t tags_remove_label = "Removable Tags";
	text_t button_tag = "&Tag";
	text_t adjust_date_help1 =
		"Items in this date range will be modified to start at a new date and time. The time gap between items will be preserved.";
	text_t adjust_date_help2 =
		"For example, if you have 2 photos taken one hour apart, after the update they will still be one hour apart.";
	text_t adjust_date_help3 =
		"This is useful if your camera date was set wrongly and you need to fix photo and video dates.";
	text_t selected_date_range_label = "Selected items are in the date range:";
	text_t starting_fmt = "Starting {}";
	text_t ending_fmt = "Ending {}";
	text_t starting_date_label = "New Starting Date (local time):";
	text_t command_rate = "Rate";
	text_t is_not_valid_folder_fmt = "'{}' is not a valid folder.";
	text_t title_updating = "Updating...";
	text_t open_with_app_tool = "Open with app or tool";
	text_t open_with_tool = "tool";
	text_t open_with_app = "app";
	text_t import_info =
		"Copy (or move) files into a structured folder hierarchy. The folder structure can be based on file metadata templates such year/date or album/artist. This is useful if you want to organize files in a structured hierarchy.";
	text_t import_dest_folder = "Destination root folder. Structured folders will be created under this folder.";
	text_t import_ignore_previous = "Ignore previously imported items.";
	text_t import_overwrite_if_newer = "Overwrite if source items are newer.";
	text_t import_dest_folder_structure = "Destination folder structure:";
	text_t import_rename_different_attributes = "Rename if files have same name but different attributes.";
	text_t move_items = "Move source items.";
	text_t import_set_created_date = "Set file date created to metadata date created.";
	text_t button_import = "&Import";
	text_t eject_help = "To avoid losing data, eject an external hard drive or USB drive before removing it.";
	text_t eject_failed_fmt = "Failed to eject {}.";
	text_t close = "Close";
	text_t eject_close_info = "Close without ejecting";
	text_t eject_title_fmt = "Eject {}";
	text_t space_used = "used";
	text_t scan_failed = "Failed to Scan.";
	text_t update_title = "Updates";
	text_t update_help_fmt = "Version {} of Diffractor is available. You are currently using version {}.";
	text_t update = "Update";
	text_t update_install_now = "Install now";
	text_t update_checking = "Checking for updates...";
	text_t update_up_to_date_fmt = "You are using the latest version ({}).";
	text_t update_help = "Download and install now. Diffractor will need to be closed briefly during the install.";
	text_t update_not_now = "Not now";
	text_t update_not_now_help = "Remind me again in a week";
	text_t update_more_info = "More info";
	text_t update_more_info_help = "Learn more about this update at Diffractor.com";
	text_t update_please_wait = "Please wait while the update is downloaded...";
	text_t update_failed = "Update failed.";
	text_t donate = "Donate";
	text_t donate_help =
		"Diffractor can be used for free. If you find it useful, please help the project by donating.";
	text_t donate_link = "Donate online.";
	text_t maybe_later = "OK - maybe later.";
	text_t error_internet =
		"Could be a problem with the internet connection? Try again later if that seems likely.";
	text_t failed_connect_internet = "Failed to connect to the internet";
	text_t documentation = "Documentation";
	text_t keyboard = "Keyboard";
	text_t support = "Support";
	text_t ok_got_it = "OK - got it.";
	text_t ask_question = "Ask the Diffractor team a question.";
	text_t list_of_accelerators = "List of keyboard accelerators";
	text_t learn_more_diffractor_com = "Learn more at Diffractor.com";
	text_t ok_all_done = "OK - all done.";
	text_t optimize = "Optimize";
	text_t defragment_and_compact = "Defragment database.";
	text_t reset_database = "Clean database and reindex.\nAll data is regenerated.";
	text_t need_some_maintenance = "After long periods of use, the index may need some maintenance.";
	text_t keyboard_basics_title = "Basics";
	text_t keyboard_navigation_title = "Navigation";
	text_t keyboard_playback_title = "Media playback";
	text_t keyboard_tools_title = "Tools";
	text_t keyboard_open_title = "Open";
	text_t keyboard_file_management_title = "File management";
	text_t keyboard_rate_label_title = "Rate or Label";
	text_t keyboard_selection_title = "Selection";
	text_t keyboard_help_title = "Help";
	text_t keyboard_options_title = "Options";
	text_t keyboard_ref_title = "Keyboard reference";
	text_t keyboard_enter = "enter";
	text_t keyboard_enter_desc = "Opens an image full screen.";
	text_t keyboard_space = "space";
	text_t keyboard_space_desc = "Stops or starts a slideshow or playing a video/audio file.";
	text_t keyboard_escape = "escape";
	text_t keyboard_escape_desc = "Gets you out of full screen, zoom or other modes.";
	text_t keyboard_left = "left";
	text_t keyboard_right = "right";
	text_t keyboard_back = "back";
	text_t keyboard_browser_back = "browser-back";
	text_t keyboard_browser_favorites = "browser-favorites";
	text_t keyboard_browser_forward = "browser-forward";
	text_t keyboard_browser_home = "browser-home";
	text_t keyboard_browser_refresh = "browser-refresh";
	text_t keyboard_browser_search = "browser-search";
	text_t keyboard_browser_stop = "browser-stop";
	text_t keyboard_del = "del";
	text_t keyboard_down = "down";
	text_t keyboard_f1 = "F1";
	text_t keyboard_f10 = "F10";
	text_t keyboard_f11 = "F11";
	text_t keyboard_f2 = "F2";
	text_t keyboard_f3 = "F3";
	text_t keyboard_f4 = "F4";
	text_t keyboard_f5 = "F5";
	text_t keyboard_f6 = "F6";
	text_t keyboard_f7 = "F7";
	text_t keyboard_f8 = "F8";
	text_t keyboard_f9 = "F9";
	text_t keyboard_insert = "insert";
	text_t keyboard_media_next_track = "media-next-track";
	text_t keyboard_media_play_pause = "media-play-pause";
	text_t keyboard_media_prev_track = "media-prev-track";
	text_t keyboard_media_stop = "media-stop";
	text_t keyboard_next = "page-down";
	text_t keyboard_oem_4 = "[";
	text_t keyboard_oem_6 = "]";
	text_t keyboard_oem_plus = "+";
	text_t keyboard_prior = "page-up";
	text_t keyboard_tab = "tab";
	text_t keyboard_up = "up";
	text_t keyboard_volume_down = "volume-down";
	text_t keyboard_volume_mute = "volume-mute";
	text_t keyboard_volume_up = "volume-up";
	text_t keyboard_left_right_desc = "Move to the next or previous item.";
	text_t keyboard_ctrl_left_right_desc = "Extend the current item selection and allow comparing of items.";
	text_t about_info =
		"Diffractor provides simple tools to make you more productive when sorting, comparing and organizing photos or videos.";
	text_t defragmenting = "Defragmenting...";
	text_t resetting = "Cleaning...";
	text_t options_app_options = "Application options";
	text_t options_save_options = "When saving";
	text_t options_updates = "Updates";
	text_t options_advanced = "Advanced";
	text_t index_maintenance_title = "Maintenance";
	text_t options_show_rotated = "Show photos rotated based on recorded camera orientation";
	text_t options_show_hidden = "Show hidden files";
	text_t options_confirm_del = "Confirm when deleting items. Deleted items are moved to the recycle bin.";
	text_t option_overwrite_gps =
		"Overwrite GPS coordinates when setting a location. Otherwise only place, state and country names are updated.";
	text_t option_slideshow_title = "Slideshow";
	text_t option_slideshow_delay = "Delay before showing the next item (in seconds)";
	text_t options_check_for_update = "Check for updates by connecting to the internet";
	text_t options_use_gpu = "Use hardware acceleration to draw the Diffractor user interface (requires restart)";
	text_t options_use_gpu_video = "Use hardware acceleration to decode video (when available)";
	text_t options_use_yuv_tex =
		"Use NV12 and P010 format textures (Automatically turned off if problems detected)";
	text_t options_send_crash_reports =
		"Help make Diffractor better. If Diffractor crashes send anonymous diagnostics to Diffractor HQ. No personal data is sent - just crash diagnostics.";
	text_t options_show_debug_info = "Show application debugging information (for the programmers).";
	text_t options_show_shadow = "Show an inset shadow surrounding the image view panel.";
	text_t options_update_modified = "Update file date modified when editing.";
	text_t options_last_played_pos = "Resume playing media from the last position played.";
	text_t options_show_help_tooltips = "Show tooltips with help on buttons and links.";
	text_t index_maintenance_help =
		"Defragmenting or cleaning can improve performance. The index stores a copy of metadata from your media files. It can be rebuilt from the original files.";
	text_t index_maintenance_reset_recommended =
		"The index is reporting errors. Restarting Diffractor is recommended.";
	text_t collection_options_more_folders = "More folders. Separate each folder with a new line.";
	text_t collection_options_local_folders_title = "Collection folders";
	text_t collection_options_pictures = "Pictures";
	text_t collection_options_videos = "Videos";
	text_t collection_options_music = "Music";
	text_t collection_options_desktop = "Desktop";
	text_t collection_options_downloads = "Downloads";
	text_t collection_options_documents = "Documents";
	text_t collection_options_dropbox = "Local DropBox Folder";
	text_t collection_options_onedrive = "Local OneDrive Folder";
	text_t collection_options_google_drive = "Local GoogleDrive Folder";
	text_t collection_options_custom_folders_title = "More Collection folders";
	text_t collection_options_custom_folders_help =
		"Prefix the folder name with minus '-' to exclude it from the collection. You exclude folders by the full path -c:\\collection\\secret, with a folder name -secret, or with a wildcard -secret*.";

	text_t collection_options_custom_locations_help =
		"Folders can be drives, folders, network shares or device labels. Labels are useful for removable or network devices where mapped drive letters can change.";
	text_t customise_tags_title = "Favorite Tags";
	text_t customise_tags_help = "Add a list of your favorite tags here to save time when tagging.";
	text_t customise_searches_title = "Favorite Searches";
	text_t customise_sidebar_title = "Sidebar";
	text_t customise_sidebar_desc = "Define what items are shown in the sidebar.";
	text_t customize_show_total = "Show total items pie chart";
	text_t customize_show_history = "Show history chart";
	text_t customize_show_world_map = "Show world map";
	text_t customize_show_indexed_folders = "Show indexed folders";
	text_t customize_show_drives = "Show drives";
	text_t customize_show_searches = "Show favorite searches";
	text_t customize_show_tags = "Show tags";
	text_t customize_ratings = "Show ratings";
	text_t customize_labels = "Show labels";
	text_t email_small_help =
		"Email sizes can be reduced using the following options. Smaller emails are faster to send.";
	text_t email_zip = "Zip files in the email";
	text_t email_convert_to_jpeg = "Convert all photos to JPEG";
	text_t email_limit_dimensions =
		"Limit photos to a maximum dimension (in pixels). Only supported for JPEG and PNG files.";
	text_t button_send = "&Send";
	text_t email_processing_fmt = "Processing {}";
	text_t email_connecting_to_mapi = "Connecting to MAPI...";
	text_t email_sending = "Sending email using MAPI...";
	text_t email_failed = "Failed to send email using MAPI.";
	text_t keyboard_or = "OR";
	text_t keyboard_alt = "alt";
	text_t keyboard_control = "ctrl";
	text_t keyboard_shift = "shift";
	text_t keyboard_backspace = "backspace";
	text_t open_with_fmt = "Open with {}";
	text_t open_with_title = "Open";
	text_t open_with_failed = "Failed to open selected items.";
	text_t tag_selected = "Tag selected items";
	text_t group_sort_tooltip = "Group and sort items";
	text_t group_sort_click = "Click to group or sort";
	text_t listed = "Listed";
	text_t listed_sub_folders = "Including sub folders:";
	text_t repeat_help = "When one media item plays to the end, start playing the next automatically";
	text_t repeat_one_help = "Repeat play single media item";
	text_t repeat_off_help = "Don't repeat or auto play";
	text_t update_available = "Update available";
	text_t update_avail_version_fmt = "Version {} of Diffractor is available.";
	text_t update_current_version_fmt = "You are currently using version {}.";
	text_t keyboard_accelerator_press = "press";
	text_t invalid = "invalid";
	text_t dates_title = "Dates";
	text_t dates_metadata_created = "Metadata created:";
	text_t dates_file_created = "File created:";
	text_t dates_file_modified = "File modified:";
	text_t stream_name_fmt = "Stream {}";
	text_t stream_select_fmt = "Click to select stream '{}'";
	text_t copy_to_join = "to";
	text_t save_new_photo = "Save new photo";
	text_t open_title = "Open";
	text_t map_instructions = "Scroll map to select a location. Use mouse-wheel to zoom.";
	text_t command_filter_photos = "Photo filter";
	text_t command_filter_videos = "Video filter";
	text_t command_filter_audio = "Audio filter";
	text_t click_to_search = "Click to search in the collection";
	text_t click_to_open = "Click to open";
	text_t collection_contains = "The collection contains {} {} ({}).";
	text_t collection_contains2 = "The collection contains:";
	text_t items_created_fmt = "Items created {} {}";
	text_t items_modified_fmt = "Items modified {} {}";
	text_t click_to_open_created_modified = "Click to open created items.\nCtrl+click to open modified items.";
	text_t click_items_from_fmt = "{} items from {} in the collection.";
	text_t filter = "filter";

	text_t genre_a_capella = "A capella";
	text_t genre_abstract = "Abstract";
	text_t genre_acidjazz = "Acid Jazz";
	text_t genre_acidpunk = "Acid Punk";
	text_t genre_acid = "Acid";
	text_t genre_acoustic = "Acoustic";
	text_t genre_action_adventure = "Action & Adventure";
	text_t genre_action = "Action";
	text_t genre_aerial = "Aerial";
	text_t genre_alternative = "Alternative";
	text_t genre_alternrock = "AlternRock";
	text_t genre_ambient = "Ambient";
	text_t genre_analog = "Analog";
	text_t genre_animation = "Animation";
	text_t genre_anime = "Anime";
	text_t genre_architectural = "Architectural";
	text_t genre_avantgarde = "Avantgarde";
	text_t genre_aviation = "Aviation";
	text_t genre_ballad = "Ballad";
	text_t genre_bass = "Bass";
	text_t genre_bebob = "Bebob";
	text_t genre_bigband = "Big Band";
	text_t genre_bluegrass = "Bluegrass";
	text_t genre_blues = "Blues";
	text_t genre_booty_bass = "Booty Bass";
	text_t genre_brazilian = "Brazilian";
	text_t genre_cabaret = "Cabaret";
	text_t genre_candid = "Candid";
	text_t genre_celtic = "Celtic";
	text_t genre_chambermusic = "Chamber Music";
	text_t genre_chanson = "Chanson";
	text_t genre_childrens = "Children's";
	text_t genre_chorus = "Chorus";
	text_t genre_christian_gospel = "Christian & Gospel";
	text_t genre_christianran_reap = "Christian Rap";
	text_t genre_classic_rock = "Classic Rock";
	text_t genre_classic = "Classic";
	text_t genre_classical = "Classical";
	text_t genre_close_up = "Close-up";
	text_t genre_cloudscape = "Cloudscape";
	text_t genre_club = "Club";
	text_t genre_comedy = "Comedy";
	text_t genre_conceptual = "Conceptual";
	text_t genre_concert_films = "Concert Films";
	text_t genre_concert = "Concert";
	text_t genre_conservation = "Conservation";
	text_t genre_country = "Country//genre";
	text_t genre_cult = "Cult";
	text_t genre_dance_hall = "Dance Hall";
	text_t genre_dance = "Dance";
	text_t genre_darkwave = "Darkwave";
	text_t genre_death_metal = "Death Metal";
	text_t genre_disco = "Disco";
	text_t genre_documentary = "Documentary";
	text_t genre_drama = "Drama";
	text_t genre_dream = "Dream";
	text_t genre_drum_solo = "Drum Solo";
	text_t genre_duet = "Duet";
	text_t genre_easylistening = "Easy Listening";
	text_t genre_electronic = "Electronic";
	text_t genre_ethnic = "Ethnic";
	text_t genre_euro_dance = "Eurodance";
	text_t genre_euro_house = "Euro-House";
	text_t genre_euro_techno = "Euro-Techno";
	text_t genre_family = "Family";
	text_t genre_fashion = "Fashion";
	text_t genre_fast_fusion = "Fast Fusion";
	text_t genre_film_still = "Film still";
	text_t genre_fine_art = "Fine-art";
	text_t genre_fire = "Fire";
	text_t genre_fireworks = "Fireworks";
	text_t genre_fitness_workout = "Fitness & Workout";
	text_t genre_folk = "Folk";
	text_t genre_folklore = "Folklore";
	text_t genre_folk_rock = "Folk-Rock";
	text_t genre_food = "Food";
	text_t genre_foreign = "Foreign";
	text_t genre_forensic = "Forensic";
	text_t genre_freestyle = "Freestyle";
	text_t genre_funk = "Funk";
	text_t genre_fusion = "Fusion";
	text_t genre_game = "Game//genre";
	text_t genre_gangsta = "Gangsta";
	text_t genre_geophotography = "Geo-photography";
	text_t genre_glamour = "Glamour";
	text_t genre_gospel = "Gospel";
	text_t genre_gothic_rock = "Gothic Rock";
	text_t genre_gothic = "Gothic";
	text_t genre_grunge = "Grunge";
	text_t genre_hardrock = "Hard Rock";
	text_t genre_highke = "High key";
	text_t genre_high_speed = "High-speed";
	text_t genre_hip_hop = "Hip-Hop";
	text_t genre_hip_hop_rap = "Hip-Hop/Rap";
	text_t genre_holiday = "Holiday";
	text_t genre_horror = "Horror";
	text_t genre_house = "House";
	text_t genre_humour = "Humour";
	text_t genre_independent = "Independent";
	text_t genre_industrial = "Industrial";
	text_t genre_instrumental_pop = "Instrumental Pop";
	text_t genre_instrumental_rock = "Instrumental Rock";
	text_t genre_instrumental = "Instrumental";
	text_t genre_jazz = "Jazz";
	text_t genre_jazzfun = "Jazz + Funk";
	text_t genre_jungle = "Jungle";
	text_t genre_kids_family = "Kids & Family";
	text_t genre_kids = "Kids";
	text_t genre_kirlian = "Kirlian";
	text_t genre_landscape = "Landscape";
	text_t genre_latin = "Latin";
	text_t genre_lifestyle = "Lifestyle";
	text_t genre_lo_fi = "Lo-fi";
	text_t genre_lomography = "Lomography";
	text_t genre_long_exposure = "Long-exposure";
	text_t genre_low_key = "Low key";
	text_t genre_macro = "Macro";
	text_t genre_medical = "Medical";
	text_t genre_meditative = "Meditative";
	text_t genre_metal = "Metal";
	text_t genre_monochrome = "Monochrome";
	text_t genre_music_documentaries = "Music Documentaries";
	text_t genre_music_feature_films = "Music Feature Films";
	text_t genre_musical = "Musical";
	text_t genre_musicals = "Musicals";
	text_t genre_narrative = "Narrative";
	text_t genre_nationa_folk = "National Folk";
	text_t genre_native_american = "Native American";
	text_t genre_new_age = "New Age";
	text_t genre_new_wave = "New Wave";
	text_t genre_night = "Night";
	text_t genre_noise = "Noise";
	text_t genre_nonfiction = "Nonfiction";
	text_t genre_oldies = "Oldies";
	text_t genre_opera = "Opera";
	text_t genre_other = "Other";
	text_t genre_panorama = "Panorama";
	text_t genre_panoramic = "Panoramic";
	text_t genre_pellier_noir = "Pellier Noir";
	text_t genre_photo_op = "Photo op";
	text_t genre_photobiography = "Photobiography";
	text_t genre_photojournalism = "Photojournalism";
	text_t genre_photowalking = "Photowalking";
	text_t genre_podcast = "Podcast";
	text_t genre_polaroid = "Polaroid";
	text_t genre_polka = "Polka";
	text_t genre_pop = "Pop";
	text_t genre_pop_funk = "Pop/Funk";
	text_t genre_pop_folk = "Pop-Folk";
	text_t genre_porn_groove = "Porn Groove";
	text_t genre_portrait = "Portrait";
	text_t genre_power_ballad = "Power Ballad";
	text_t genre_pranks = "Pranks";
	text_t genre_primus = "Primus";
	text_t genre_progressive_rock = "Progressive Rock";
	text_t genre_psychadelic = "Psychadelic";
	text_t genre_psychedelic_rock = "Psychedelic Rock";
	text_t genre_punk_rock = "Punk Rock";
	text_t genre_punk = "Punk";
	text_t genre_randb = "R&B";
	text_t genre_rap = "Rap";
	text_t genre_rave = "Rave";
	text_t genre_reality_tv = "Reality TV";
	text_t genre_reggae = "Reggae";
	text_t genre_retro = "Retro";
	text_t genre_revival = "Revival";
	text_t genre_rhythmic_soul = "Rhythmic Soul";
	text_t genre_rock_and_roll = "Rock & Roll";
	text_t genre_rock = "Rock";
	text_t genre_romance = "Romance";
	text_t genre_samba = "Samba";
	text_t genre_satellite = "Satellite";
	text_t genre_satire = "Satire";
	text_t genre_scifi_and_fantasy = "Sci-Fi & Fantasy";
	text_t genre_short_films = "Short Films";
	text_t genre_show_tunes = "Show tunes";
	text_t genre_singer_songwriter = "Singer/Songwriter";
	text_t genre_ska = "Ska";
	text_t genre_slow_jam = "Slow Jam";
	text_t genre_slow_rock = "Slow Rock";
	text_t genre_social = "Social";
	text_t genre_soft_focus = "Soft focus";
	text_t genre_sonata = "Sonata";
	text_t genre_soul = "Soul";
	text_t genre_sound_clip = "Sound Clip";
	text_t genre_soundtrack = "Soundtrack";
	text_t genre_southern_rock = "Southern Rock";
	text_t genre_space = "Space";
	text_t genre_special_interest = "Special Interest";
	text_t genre_speech = "Speech";
	text_t genre_sports = "Sports";
	text_t genre_star_trail = "Star trail";
	text_t genre_still_life = "Still life";
	text_t genre_stock = "Stock";
	text_t genre_street = "Street";
	text_t genre_subminiature = "Subminiature";
	text_t genre_swing = "Swing";
	text_t genre_symphonic_rock = "Symphonic Rock";
	text_t genre_symphony = "Symphony";
	text_t genre_tango = "Tango";
	text_t genre_techno = "Techno";
	text_t genre_techno_industrial = "Techno-Industrial";
	text_t genre_teens = "Teens";
	text_t genre_thriller = "Thriller";
	text_t genre_time_lapse = "Time-lapse";
	text_t genre_top40 = "Top 40";
	text_t genre_trailer = "Trailer";
	text_t genre_trance = "Trance";
	text_t genre_travel = "Travel";
	text_t genre_tribal = "Tribal";
	text_t genre_trip_hop = "Trip-Hop";
	text_t genre_ultraviolet = "Ultraviolet";
	text_t genre_underwater = "Underwater";
	text_t genre_unknown = "Unknown";
	text_t genre_urban = "Urban";
	text_t genre_vernacular = "Vernacular";
	text_t genre_vintage = "Vintage";
	text_t genre_vocal = "Vocal";
	text_t genre_war = "War";
	text_t genre_western = "Western";
	text_t genre_world = "World";
	text_t search_last_7_days = "Last 7 days";
	text_t search_top_rated = "Top rated";
	text_t search_christmas = "Christmas";
	text_t install_welcome_text1 = "Super-fast searching, viewing and comparing of photos or videos.";
	text_t install_welcome_text2 = "Optimized for your graphics card and PC.";
	text_t total_title = "{} ({}) total items in {} folders.";
	text_t index_size_fmt = "Currently the index database is {} in size and contains metadata from {} items.";
	text_t count = "Count";
	text_t folder = "folder";
	text_t folders = "folders";
	text_t photo = "photo";
	text_t photos = "photos";
	text_t video = "video";
	text_t data = "data";
	text_t subtitle = "subtitle";
	text_t videos = "videos";
	text_t audio = "audio";
	text_t audio_title = "Audio";
	text_t archive = "archive";
	text_t archives = "archives";
	text_t retro = "retro";
	text_t retro_title = "Retro";
	text_t other = "other";
	text_t others = "others";
	text_t truncated_at_one_mb = "Truncated at 1MB";
	text_t help_more_info = "Help or more information";
	text_t help_send_info = "Send anonymous log information to Diffractor HQ.";
	text_t click_collection_options = "Click for collection options";
	text_t collection_title = "Your collection";
	text_t location_title = "Location";
	text_t size_title = "Size";
	text_t customize_display_title = "Display";
	text_t presence_tile = "Presence";
	text_t presence_loading = "Presence loading...";
	text_t presence_not_in = "NOT in collection";
	text_t presence_this_in = "In collection";
	text_t presence_similar_in = "Similar in collection";
	text_t presence_newer_in = "Newer in collection";
	text_t presence_older_in = "Older in collection";
	text_t presence_not_in_long = "This item is NOT in the collection.";
	text_t presence_this_in_long = "This item is in the collection.";
	text_t presence_similar_in_long =
		"This item is outside the collection but similar item(s) exist in the collection.";
	text_t presence_newer_in_long =
		"This item is outside the collection but similar NEWER item(s) exist in the collection.";
	text_t presence_older_in_long =
		"This item is outside the collection but similar OLDER item(s) exist in the collection.";
	text_t unselect_fmt = "Unselect {}";
	text_t delete_fmt = "Delete {}";
	text_t folder_title = "Folder";
	text_t command_save_and_back_tooltip = "Save the current item and open the previous item.";
	text_t command_save_and_next_tooltip = "Save the current item and open the next item.";
	text_t editing_title = "Editing";
	text_t add_folder = "Add folder";
	text_t items_identical = "Files are identical.";
	text_t items_not_identical = "Files are different.";
	text_t pixels_identical_files_not_identical = "Pixels are identical but files are different.";
	text_t copy_to_clipboard = "Copy to clipboard";
	text_t open_link_fmt = "Open link {}";
	text_t xmp_metadata_title = "XMP";
	text_t icc_metadata_title = "ICC";
	text_t metadata_title = "Metadata";
	text_t media_metadata_title = "Media";
	text_t raw_metadata_title = "Raw";
	text_t exif_metadata_title = "EXIF";
	text_t iptc_metadata_title = "IPTC";
	text_t none = "None";
	text_t collection_options_info = "Define the folders that make your collection.";
	text_t more_collection_options_information = "More information about collection management.";
	text_t more_template_information = "More information about metadata templates.";
	text_t show_raw_now = "Show this RAW photo";
	text_t show_raw = "Always show RAW photos (slower)";
	text_t preview_show_preview = "Show RAW preview if available (faster)";
	text_t preview_showing = "Showing a RAW photo preview.";
	text_t preview_rendering = "Rendering RAW photo...";
	text_t preview_rendered = "Showing fully rendered RAW photo.";

	text_t command_favorite = "Favorite";
	text_t command_advanced_search = "Advanced Search";
	text_t favorite_add_fmt = "Add '{}' to favorites.";
	text_t favorite_remove_fmt = "Remove '{}' from favorite folders or searches.";
	text_t collection_in = "Shown items are from the collection.";
	text_t collection_not_in = "Shown items are NOT from the collection.";
	text_t collection_info =
		"The collection is a set of indexed folders. Indexing allows fast searching and duplicate detection. Diffractor will also highlight when items are not in the collection.";
	text_t favorite_title = "Favorite title";
	text_t favorite_info = "Favorites are listed in the sidebar for quick access.";
	text_t favorite_failed_to_add = "Failed to add. Maximum number of favorites reached.";

	text_t search_collection = "Search in the collection";
	text_t search_folder = "Search in a folder";
	text_t search_sub_folders = "Search in subfolders";
	text_t search_not_in_collection = "Find files not in collection";
	text_t search_all_terms = "All these terms";
	text_t search_any_terms = "Any of these terms";
	text_t search_none_terms = "None of these terms";
	text_t search_photos = "Photos";
	text_t search_videos = "Videos";
	text_t search_audio = "Audio";
	text_t search_located_within = "Located within";
	text_t search_select_term = "Select term";
	text_t search_date_from = "From";
	text_t search_date_until = "Until";
	text_t km_from = "km from";

	text_t command_sync = "Synchronize";
	text_t sync_details = "Synchronize your collection with a remote location.";
	text_t sync_collection = "Synchronize whole collection";
	text_t sync_other_folder = "Synchronize only one folder";
	text_t sync_remote_folder = "Remote folder";
	text_t sync_local_remote = "Copy newer local files to remote folders";
	text_t sync_remote_local = "Copy newer remote files to local folders";
	text_t sync_delete_local = "Delete local files that do not exist remotely";
	text_t sync_delete_remote = "Delete remote files that do not exist locally";
	text_t sync_local = "Source of synchronization (Local)";
	text_t sync_info_1 =
		"Synchronize local files to a remote location. This is useful to synchronize your local file collection to a backup location.";
	text_t sync_info_2 = "c:\\file-collection with \\\\nas\\\\backup-files";
	text_t sync_remote = "Destination of synchronization (Remote)";
	text_t sync_copy_remote_action = "copy remote";
	text_t sync_copy_local_action = "copy local";
	text_t sync_delete_remote_action = "delete remote";
	text_t sync_delete_local_action = "delete local";
	text_t button_sync = "&Synchronize";

	text_t import_from = "Source of import:";
	text_t import_src_filter = "Source files filter:";
	text_t import_other_folder = "Other folder";
	text_t import_detecting = "Detecting folders...";
	text_t button_analyze = "&Analyze";
	text_t analyze = "Analyze";
	text_t select_location = "Select a location";
	text_t import_overwrite = "Overwrite";
	text_t import_ignore = "Ignore";
	text_t selected_items_fmt = "Selected items [{}]";
	text_t analyzing = "Analyzing...";
	text_t processing = "Processing...";
	text_t scope = "Scope";
	text_t value = "Value";
	text_t all_items_filtered = "All items filtered out.\nClick to clear filter.";
	text_t items_created_on_fmt = "The collection contains {} items created on {}.";

	text_t command_all_tags = "Show all tags";
	text_t command_favorite_tags = "Show only favorite tags";
	text_t option_favorite_tags = "Show only favorite tags in the sidebar";

	text_t collection_add = "Collection";
	text_t source = "Source";
	text_t destination = "Destination";
	text_t action = "Action";
	text_t old_name = "Old name";
	text_t new_name = "New name";
	text_t local = "Local";
	text_t remote = "Remote";
	text_t status = "Status";
	text_t message = "Message";
	text_t import = "import";
	text_t exists = "exists";
	text_t previously_imported = "previously imported";
	text_t ignore = "ignore";
	text_t test = "test";
	text_t view_empty_message = "Click analyze to show items";

	plural_text rotate_info_fmt = {
		"Rotate {first-name}.", "Rotate {count} selected items. Originals will be overwritten."
	};
	plural_text title_folder_count_fmt = {"1 folder", "{count} folders"};
	plural_text title_item_count_fmt = {"{count} item", "{count} items"};
	plural_text rating_set_fmt = {"Click to rate as 1 star", "Click to rate as {count} stars"};
	plural_text cannot_process_fmt = {
		"Cannot process {first-name}.", "Cannot process {first-name} and {other} other items."
	};
	plural_text rename_fmt = {
		"{first-name} will be renamed.", "{count} items will be renamed."
	};
	plural_text dup_count_fmt = {"1 duplicate item", "{count} duplicate items"};
	plural_text sidecar_count_fmt = {"1 sidecar item", "{count} sidecar items"};
	plural_text processed_x_of_x_fmt = {
		"{count} of 1 item was processed.", "{count} of {total} items were processed."
	};
	plural_text processed_fmt = {"1 item was processed.", "{count} items were processed."};
	plural_text failed_items_fmt = {"{first-name} failed.", "{count} items failed."};
	plural_text ignored_fmt = {"{first-name} was ignored.", "{count} items were ignored."};
	plural_text ignored_exist_already_fmt = {
		"{first-name} was ignored because it exist already in the destination location.",
		"{count} items were ignored because they exist already in the destination location."
	};
	plural_text ignored_previous_fmt = {
		"{first-name} was ignored because it was previously imported.",
		"{count} items were ignored because they were previously imported."
	};
	plural_text delete_info_fmt = {
		"{first-name} will be moved to the recycle bin.",
		"{count} items ({size}) will be moved to the recycle bin."
	};
	plural_text copy_fmt = {"{first-name} will be copied.", "{count} items ({size}) will be copied."};
	plural_text move_fmt = {"{first-name} will be moved.", "{count} items ({size}) will be moved."};
	plural_text be_updated_fmt = {"{first-name} will be updated.", "{count} items will be updated."};
	plural_text edit_metadata_fmt = {
		"Add or overwrite specific metadata in {first-name}.",
		"Add or overwrite specific metadata in {count} items."
	};
	plural_text convert_info_fmt = {
		"{first-name} will be converted and copied to a folder.",
		"{count} items will be converted and copied to a folder."
	};
	plural_text tag_info_fmt = {
		"{first-name} will have tags added or removed.", "{count} items will have tags added or removed."
	};
	plural_text adjust_date_info_fmt = {
		"{first-name} will have the date adjusted.", "{count} items will have the date adjusted."
	};
	plural_text shared_fmt = {"{first-name} will be shared.", "{count} items will be shared."};
	plural_text import_copy_fmt = {
		"{first-name} will be copied or moved.", "{count} items ({size}) will be copied or moved."
	};
	plural_text email_info_fmt = {
		"{first-name} will be sent using your desktop email client.",
		"{count} items will be sent using your desktop email client."
	};
	plural_text would_overwrite_fmt = {
		"{first-name} already exists. Do you want to replace it?",
		"{first-name} and {other} other items already exist. Do you want to replace them?"
	};
	plural_text gps_overwrite_count_fmt = {
		"{first-name} will have existing GPS position metadata overwritten.",
		"{first-name} and {other} other items will have existing GPS position metadata overwritten."
	};
};

extern app_text_t tt;
