// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: IPTC metadata parsing. Extracts news industry metadata including
// captions, keywords, copyright, and geographic information.

#include "pch.h"
#include "metadata_iptc.h"
#include "model_tags.h"
#include "model_property.h"

enum iptc_record
{
	IPTC_RECORD_OBJECT_ENV = 1,
	IPTC_RECORD_APP_2 = 2,
	IPTC_RECORD_APP_3 = 3,
	IPTC_RECORD_APP_4 = 4,
	IPTC_RECORD_APP_5 = 5,
	IPTC_RECORD_APP_6 = 6,
	IPTC_RECORD_PREOBJ_DATA = 7,
	IPTC_RECORD_OBJ_DATA = 8,
	IPTC_RECORD_POSTOBJ_DATA = 9
};

enum iptc_tag
{
	IPTC_TAG_MODEL_VERSION = 0,
	/* Begin record 1 tags */
	IPTC_TAG_DESTINATION = 5,
	IPTC_TAG_FILE_FORMAT = 20,
	IPTC_TAG_FILE_VERSION = 22,
	IPTC_TAG_SERVICE_ID = 30,
	IPTC_TAG_ENVELOPE_NUM = 40,
	IPTC_TAG_PRODUCT_ID = 50,
	IPTC_TAG_ENVELOPE_PRIORITY = 60,
	IPTC_TAG_DATE_SENT = 70,
	IPTC_TAG_TIME_SENT = 80,
	IPTC_TAG_CHARACTER_SET = 90,
	IPTC_TAG_UNO = 100,
	IPTC_TAG_ARM_ID = 120,
	IPTC_TAG_ARM_VERSION = 122,
	/* End record 1 tags */
	IPTC_TAG_RECORD_VERSION = 0,
	/* Begin record 2 tags */
	IPTC_TAG_OBJECT_TYPE = 3,
	IPTC_TAG_OBJECT_ATTRIBUTE = 4,
	IPTC_TAG_OBJECT_NAME = 5,
	IPTC_TAG_EDIT_STATUS = 7,
	IPTC_TAG_EDITORIAL_UPDATE = 8,
	IPTC_TAG_URGENCY = 10,
	IPTC_TAG_SUBJECT_REFERENCE = 12,
	IPTC_TAG_CATEGORY = 15,
	IPTC_TAG_SUPPL_CATEGORY = 20,
	IPTC_TAG_FIXTURE_ID = 22,
	IPTC_TAG_KEYWORDS = 25,
	IPTC_TAG_CONTENT_LOC_CODE = 26,
	IPTC_TAG_CONTENT_LOC_NAME = 27,
	IPTC_TAG_RELEASE_DATE = 30,
	IPTC_TAG_RELEASE_TIME = 35,
	IPTC_TAG_EXPIRATION_DATE = 37,
	IPTC_TAG_EXPIRATION_TIME = 38,
	IPTC_TAG_SPECIAL_INSTRUCTIONS = 40,
	IPTC_TAG_ACTION_ADVISED = 42,
	IPTC_TAG_REFERENCE_SERVICE = 45,
	IPTC_TAG_REFERENCE_DATE = 47,
	IPTC_TAG_REFERENCE_NUMBER = 50,
	IPTC_TAG_DATE_CREATED = 55,
	IPTC_TAG_TIME_CREATED = 60,
	IPTC_TAG_DIGITAL_CREATION_DATE = 62,
	IPTC_TAG_DIGITAL_CREATION_TIME = 63,
	IPTC_TAG_ORIGINATING_PROGRAM = 65,
	IPTC_TAG_PROGRAM_VERSION = 70,
	IPTC_TAG_OBJECT_CYCLE = 75,
	IPTC_TAG_BYLINE = 80,
	IPTC_TAG_BYLINE_TITLE = 85,
	IPTC_TAG_CITY = 90,
	IPTC_TAG_SUBLOCATION = 92,
	IPTC_TAG_STATE = 95,
	IPTC_TAG_COUNTRY_CODE = 100,
	IPTC_TAG_COUNTRY_NAME = 101,
	IPTC_TAG_ORIG_TRANS_REF = 103,
	IPTC_TAG_HEADLINE = 105,
	IPTC_TAG_CREDIT = 110,
	IPTC_TAG_SOURCE = 115,
	IPTC_TAG_COPYRIGHT_NOTICE = 116,
	IPTC_TAG_PICASA_UNKNOWN = 117,
	IPTC_TAG_CONTACT = 118,
	IPTC_TAG_FLICKRID = 219,
	IPTC_TAG_CAPTION = 120,
	IPTC_TAG_WRITER_EDITOR = 122,
	IPTC_TAG_RASTERIZED_CAPTION = 125,
	IPTC_TAG_IMAGE_TYPE = 130,
	IPTC_TAG_IMAGE_ORIENTATION = 131,
	IPTC_TAG_LANGUAGE_ID = 135,
	IPTC_TAG_AUDIO_TYPE = 150,
	IPTC_TAG_AUDIO_SAMPLING_RATE = 151,
	IPTC_TAG_AUDIO_SAMPLING_RES = 152,
	IPTC_TAG_AUDIO_DURATION = 153,
	IPTC_TAG_AUDIO_OUTCUE = 154,
	IPTC_TAG_PREVIEW_FORMAT = 200,
	IPTC_TAG_PREVIEW_FORMAT_VER = 201,
	IPTC_TAG_PREVIEW_DATA = 202,
	/* End record 2 tags */
	IPTC_TAG_SIZE_MODE = 10,
	/* Begin record 7 tags */
	IPTC_TAG_MAX_SUBFILE_SIZE = 20,
	IPTC_TAG_SIZE_ANNOUNCED = 90,
	IPTC_TAG_MAX_OBJECT_SIZE = 95,
	/* End record 7 tags */
	IPTC_TAG_SUBFILE = 10,
	/* Record 8 tags */
	IPTC_TAG_CONFIRMED_DATA_SIZE = 10 /* Record 9 tags */
};

enum iptc_format
{
	IPTC_FORMAT_UNKNOWN,
	IPTC_FORMAT_BINARY,
	IPTC_FORMAT_BYTE,
	IPTC_FORMAT_SHORT,
	IPTC_FORMAT_LONG,
	IPTC_FORMAT_STRING,
	IPTC_FORMAT_NUMERIC_STRING,
	IPTC_FORMAT_DATE,
	IPTC_FORMAT_TIME
};


enum iptc_mandatory
{
	IPTC_OPTIONAL = 0,
	IPTC_MANDATORY = 1
};

enum iptc_repeatable
{
	IPTC_NOT_REPEATABLE = 0,
	IPTC_REPEATABLE = 1
};

struct iptc_tag_info
{
	iptc_record record;
	iptc_tag tag;
	std::string_view name;
	std::string_view title;
	std::string_view description;
	iptc_format format;
	iptc_mandatory mandatory;
	iptc_repeatable repeatable;
	uint32_t minbytes;
	uint32_t maxbytes;
};

static const iptc_tag_info iptc_tag_table[] = {
	{
		IPTC_RECORD_OBJECT_ENV, IPTC_TAG_MODEL_VERSION,
		"ModelVersion",
		"Model Version",
		"Version of IIM part 1.",
		IPTC_FORMAT_SHORT, IPTC_MANDATORY, IPTC_NOT_REPEATABLE, 2, 2
	},
	{
		IPTC_RECORD_OBJECT_ENV, IPTC_TAG_DESTINATION,
		"Destination",
		"Destination",
		"Routing information.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_REPEATABLE, 0, 1024
	},
	{
		IPTC_RECORD_OBJECT_ENV, IPTC_TAG_FILE_FORMAT,
		"FileFormat",
		"File Format",
		"File format of the data described by this metadata.",
		IPTC_FORMAT_SHORT, IPTC_MANDATORY, IPTC_NOT_REPEATABLE, 2, 2
	},
	{
		IPTC_RECORD_OBJECT_ENV, IPTC_TAG_FILE_VERSION,
		"FileVersion",
		"File Version",
		"Version of the file format.",
		IPTC_FORMAT_SHORT, IPTC_MANDATORY, IPTC_NOT_REPEATABLE, 2, 2
	},
	{
		IPTC_RECORD_OBJECT_ENV, IPTC_TAG_SERVICE_ID,
		"ServiceID",
		"Service Identifier",
		"Identifies the provider and product.",
		IPTC_FORMAT_STRING, IPTC_MANDATORY, IPTC_NOT_REPEATABLE, 0, 10
	},
	{
		IPTC_RECORD_OBJECT_ENV, IPTC_TAG_ENVELOPE_NUM,
		"EnvelopeNum",
		"Envelope Number",
		"A number unique for the date in 1:70 and the service ID in 1:30.",
		IPTC_FORMAT_NUMERIC_STRING, IPTC_MANDATORY, IPTC_NOT_REPEATABLE, 8, 8
	},
	{
		IPTC_RECORD_OBJECT_ENV, IPTC_TAG_PRODUCT_ID,
		"ProductID",
		"Product I.D.",
		"Allows a provider to identify subsets of its overall service.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_REPEATABLE, 0, 32
	},
	{
		IPTC_RECORD_OBJECT_ENV, IPTC_TAG_ENVELOPE_PRIORITY,
		"EnvelopePriority",
		"Envelope Priority",
		"Specifies the envelope handling priority and not the editorial urgency.  '1' for most urgent, '5' for normal, and '8' for least urgent.  '9' is user-defined.",
		IPTC_FORMAT_NUMERIC_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 1, 1
	},
	{
		IPTC_RECORD_OBJECT_ENV, IPTC_TAG_DATE_SENT,
		"DateSent",
		"Date Sent",
		"The day the service sent the material.",
		IPTC_FORMAT_DATE, IPTC_MANDATORY, IPTC_NOT_REPEATABLE, 8, 8
	},
	{
		IPTC_RECORD_OBJECT_ENV, IPTC_TAG_TIME_SENT,
		"TimeSent",
		"Time Sent",
		"The time the service sent the material.",
		IPTC_FORMAT_TIME, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 11, 11
	},
	{
		IPTC_RECORD_OBJECT_ENV, IPTC_TAG_CHARACTER_SET,
		"CharacterSet",
		"Coded Character Set",
		"Control functions used for the announcement, invocation or designation of coded character sets.",
		IPTC_FORMAT_BINARY, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 32
	},
	{
		IPTC_RECORD_OBJECT_ENV, IPTC_TAG_UNO,
		"UNO",
		"Unique Name of Object",
		"An eternal, globally unique identification for the object, independent of provider and for any media form.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 14, 80
	},
	{
		IPTC_RECORD_OBJECT_ENV, IPTC_TAG_ARM_ID,
		"ARMID",
		"ARM Identifier",
		"Identifies the Abstract Relationship Method (ARM).",
		IPTC_FORMAT_SHORT, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 2, 2
	},
	{
		IPTC_RECORD_OBJECT_ENV, IPTC_TAG_ARM_VERSION,
		"ARMVersion",
		"ARM Version",
		"Identifies the version of the Abstract Relationship Method (ARM).",
		IPTC_FORMAT_SHORT, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 2, 2
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_RECORD_VERSION,
		"RecordVersion",
		"Record Version",
		"Identifies the version of the IIM, Part 2",
		IPTC_FORMAT_SHORT, IPTC_MANDATORY, IPTC_NOT_REPEATABLE, 2, 2
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_OBJECT_TYPE,
		"ObjectType",
		"Object Type Reference",
		"Distinguishes between different types of objects within the IIM.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 3, 67
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_OBJECT_ATTRIBUTE,
		"ObjectAttribute",
		"Object Attribute Reference",
		"Defines the nature of the object independent of the subject.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_REPEATABLE, 4, 68
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_OBJECT_NAME,
		"ObjectName",
		"Object Name",
		"A shorthand reference for the object.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 64
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_EDIT_STATUS,
		"EditStatus",
		"Edit Status",
		"Status of the object, according to the practice of the provider.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 64
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_EDITORIAL_UPDATE,
		"EditorialUpdate",
		"Editorial Update",
		"Indicates the type of update this object provides to a previous object.  The link to the previous object is made using the ARM.  '01' indicates an additional language.",
		IPTC_FORMAT_NUMERIC_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 2, 2
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_URGENCY,
		"Urgency",
		"Urgency",
		"Specifies the editorial urgency of content and not necessarily the envelope handling priority.  '1' is most urgent, '5' normal, and '8' least urgent.",
		IPTC_FORMAT_NUMERIC_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 1, 1
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_SUBJECT_REFERENCE,
		"SubjectRef",
		"Subject Reference",
		"A structured definition of the subject matter.  It must contain an IPR, an 8 digit Subject Reference Number and an optional Subject Name, Subject Matter Name, and Subject Detail Name each separated by a colon (:).",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_REPEATABLE, 13, 236
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_CATEGORY,
		"Category",
		"Category",
		"Identifies the subject of the object in the opinion of the provider (Deprecated).",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 3
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_SUPPL_CATEGORY,
		"SupplCategory",
		"Supplemental Category",
		"Further refines the subject of the object (Deprecated).",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_REPEATABLE, 0, 32
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_FIXTURE_ID,
		"FixtureID",
		"Fixture Identifier",
		"Identifies objects that recur often and predictably, enabling users to immediately find or recall such an object.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 32
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_KEYWORDS,
		"Keywords",
		"Keywords",
		"Used to indicate specific information retrieval words.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_REPEATABLE, 0, 64
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_CONTENT_LOC_CODE,
		"ContentLocCode",
		"Content Location Code",
		"Indicates the code of a country/geographical location referenced by the content of the object.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_REPEATABLE, 3, 3
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_CONTENT_LOC_NAME,
		"ContentLocName",
		"Content Location Name",
		"A full, publishable name of a country/geographical location referenced by the content of the object.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_REPEATABLE, 0, 64
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_RELEASE_DATE,
		"ReleaseDate",
		"Release Date",
		"Designates the earliest date the provider intends the object to be used.",
		IPTC_FORMAT_DATE, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 8, 8
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_RELEASE_TIME,
		"ReleaseTime",
		"Release Time",
		"Designates the earliest time the provider intends the object to be used.",
		IPTC_FORMAT_TIME, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 11, 11
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_EXPIRATION_DATE,
		"ExpirationDate",
		"Expiration Date",
		"Designates the latest date the provider intends the object to be used.",
		IPTC_FORMAT_DATE, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 8, 8
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_EXPIRATION_TIME,
		"ExpirationTime",
		"Expiration Time",
		"Designates the latest time the provider intends the object to be used.",
		IPTC_FORMAT_TIME, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 11, 11
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_SPECIAL_INSTRUCTIONS,
		"SpecialInstructions",
		"Special Instructions",
		"Other editorial instructions concerning the use of the object.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 256
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_ACTION_ADVISED,
		"ActionAdvised",
		"Action Advised",
		"The type of action that this object provides to a previous object.  '01' Object Kill, '02' Object Replace, '03' Object Append, '04' Object Reference.",
		IPTC_FORMAT_NUMERIC_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 2, 2
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_REFERENCE_SERVICE,
		"RefService",
		"Reference Service",
		"The Service Identifier of a prior envelope to which the current object refers.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_REPEATABLE, 0, 10
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_REFERENCE_DATE,
		"RefDate",
		"Reference Date",
		"The date of a prior envelope to which the current object refers.",
		IPTC_FORMAT_DATE, IPTC_OPTIONAL, IPTC_REPEATABLE, 8, 8
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_REFERENCE_NUMBER,
		"RefNumber",
		"Reference Number",
		"The Envelope Number of a prior envelope to which the current object refers.",
		IPTC_FORMAT_NUMERIC_STRING, IPTC_OPTIONAL, IPTC_REPEATABLE, 8, 8
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_DATE_CREATED,
		"DateCreated",
		"Date Created",
		"The date the intellectual content of the object was created rather than the date of the creation of the physical representation.",
		IPTC_FORMAT_DATE, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 8, 8
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_TIME_CREATED,
		"TimeCreated",
		"Time Created",
		"The time the intellectual content of the object was created rather than the date of the creation of the physical representation.",
		IPTC_FORMAT_TIME, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 11, 11
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_DIGITAL_CREATION_DATE,
		"DigitalCreationDate",
		"Digital Creation Date",
		"The date the digital representation of the object was created.",
		IPTC_FORMAT_DATE, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 8, 8
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_DIGITAL_CREATION_TIME,
		"DigitalCreationTime",
		"Digital Creation Time",
		"The time the digital representation of the object was created.",
		IPTC_FORMAT_TIME, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 11, 11
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_ORIGINATING_PROGRAM,
		"OriginatingProgram",
		"Originating Program",
		"The type of program used to originate the object.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 32
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_PROGRAM_VERSION,
		"ProgramVersion",
		"Program Version",
		"The version of the originating program.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 10
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_OBJECT_CYCLE,
		"ObjectCycle",
		"Object Cycle",
		"Where 'a' is morning, 'b' is evening, 'b' is both.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 1, 1
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_BYLINE,
		"Byline",
		"By-line",
		"Name of the creator of the object, e.g. writer, photographer or graphic artist.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_REPEATABLE, 0, 32
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_BYLINE_TITLE,
		"BylineTitle",
		"By-line Title",
		"Title of the creator or creators of the object.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_REPEATABLE, 0, 32
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_CITY,
		"City",
		"City",
		"City of object origin.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 32
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_SUBLOCATION,
		"Sublocation",
		"Sub-location",
		"The location within a city from which the object originates.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 32
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_STATE,
		"State",
		"Province/State",
		"The Province/State where the object originates.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 32
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_COUNTRY_CODE,
		"CountryCode",
		"Country Code",
		"The code of the country/primary location where the object was created.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 3, 3
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_COUNTRY_NAME,
		"CountryName",
		"Country Name",
		"The name of the country/primary location where the object was created.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 64
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_ORIG_TRANS_REF,
		"OrigTransRef",
		"Original Transmission Reference",
		"A code representing the location of original transmission.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 32
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_HEADLINE,
		"Headline",
		"Headline",
		"A publishable entry providing a synopsis of the contents of the object.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 256
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_CREDIT,
		"Credit",
		"Credit",
		"Identifies the provider of the object, not necessarily the owner/creator.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 32
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_SOURCE,
		"Source",
		"Source",
		"The original owner of the intellectual content of the object.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 32
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_COPYRIGHT_NOTICE,
		"CopyrightNotice",
		"Copyright Notice",
		"Any necessary copyright notice.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 128
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_CONTACT,
		"Contact",
		"Contact",
		"The person or organization which can provide further background information on the object.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_REPEATABLE, 0, 128
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_CAPTION,
		"Caption",
		"Caption/Abstract",
		"A textual description of the data",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 2000
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_WRITER_EDITOR,
		"WriterEditor",
		"Writer/Editor",
		"The name of the person involved in the writing, editing or correcting the object or caption/abstract",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_REPEATABLE, 0, 32
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_RASTERIZED_CAPTION,
		"RasterizedCaption",
		"Rasterized Caption",
		"Contains rasterized object description and is used where characters that have not been coded are required for the caption.",
		IPTC_FORMAT_BINARY, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 7360, 7360
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_IMAGE_TYPE,
		"ImageType",
		"Image Type",
		"Indicates the data format of the image object.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 2, 2
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_IMAGE_ORIENTATION,
		"ImageOrientation",
		"Image Orientation",
		"The layout of the image area: 'P' for portrait, 'L' for landscape, and 'S' for square.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 1, 1
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_LANGUAGE_ID,
		"LanguageID",
		"Language Identifier",
		"The major national language of the object, according to the 2-letter codes of ISO 639:1988.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 2, 3
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_AUDIO_TYPE,
		"AudioType",
		"Audio Type",
		"The number of channels and type of audio (music, text, etc.) in the object.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 2, 2
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_AUDIO_SAMPLING_RATE,
		"AudioSamplingRate",
		"Audio Sampling Rate",
		"The sampling rate in Hz of the audio data.",
		IPTC_FORMAT_NUMERIC_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 6, 6
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_AUDIO_SAMPLING_RES,
		"AudioSamplingRes",
		"Audio Sampling Resolution",
		"The number of bits in each audio sample.",
		IPTC_FORMAT_NUMERIC_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 2, 2
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_AUDIO_DURATION,
		"AudioDuration",
		"Audio Duration",
		"The running time of the audio data in the form HHMMSS.",
		IPTC_FORMAT_NUMERIC_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 6, 6
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_AUDIO_OUTCUE,
		"AudioOutcue",
		"Audio Outcue",
		"The content at the end of the audio data.",
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 64
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_PREVIEW_FORMAT,
		"PreviewFileFormat",
		"Preview File Format",
		"Binary value indicating the file format of the object preview data in dataset 2:202.",
		IPTC_FORMAT_SHORT, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 2, 2
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_PREVIEW_FORMAT_VER,
		"PreviewFileFormatVer",
		"Preview File Format Version",
		"The version of the preview file format specified in 2:200.",
		IPTC_FORMAT_SHORT, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 2, 2
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_PREVIEW_DATA,
		"PreviewData",
		"Preview Data",
		"The object preview data",
		IPTC_FORMAT_BINARY, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 256000
	},
	{
		IPTC_RECORD_PREOBJ_DATA, IPTC_TAG_SIZE_MODE,
		"SizeMode",
		"Size Mode",
		"Set to 0 if the size of the object is known and 1 if not known.",
		IPTC_FORMAT_BYTE, IPTC_MANDATORY, IPTC_NOT_REPEATABLE, 1, 1
	},
	{
		IPTC_RECORD_PREOBJ_DATA, IPTC_TAG_MAX_SUBFILE_SIZE,
		"MaxSubfileSize",
		"Max Subfile Size",
		"The maximum size for a subfile dataset (8:10) containing a portion of the object data.",
		IPTC_FORMAT_LONG, IPTC_MANDATORY, IPTC_NOT_REPEATABLE, 4, 4
	},
	{
		IPTC_RECORD_PREOBJ_DATA, IPTC_TAG_SIZE_ANNOUNCED,
		"ObjectSizeAnnounced",
		"Object Size Announced",
		"The total size of the object data if it is known.",
		IPTC_FORMAT_LONG, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 4, 4
	},
	{
		IPTC_RECORD_PREOBJ_DATA, IPTC_TAG_MAX_OBJECT_SIZE,
		"MaxObjectSize",
		"Maximum Object Size",
		"The largest possible size of the object if the size is not known.",
		IPTC_FORMAT_LONG, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 4, 4
	},
	{
		IPTC_RECORD_OBJ_DATA, IPTC_TAG_SUBFILE,
		"Subfile",
		"Subfile",
		"The object data itself.  Subfiles must be sequential so that the subfiles may be reassembled.",
		IPTC_FORMAT_BINARY, IPTC_MANDATORY, IPTC_REPEATABLE, 0, UINT32_MAX
	},
	{
		IPTC_RECORD_POSTOBJ_DATA, IPTC_TAG_CONFIRMED_DATA_SIZE,
		"ConfirmedDataSize",
		"Confirmed Data Size",
		"Total size of the object data.",
		IPTC_FORMAT_LONG, IPTC_MANDATORY, IPTC_NOT_REPEATABLE, 4, 4
	},
	{static_cast<iptc_record>(0), static_cast<iptc_tag>(0), {}, {}, {}}
};

// IPTC records carry no dependable encoding marker, so text that is not valid UTF-8 is read as
// Latin-1 - what Photoshop and the wire services wrote for years. Without this the accented
// characters in older captions and bylines arrive as mojibake.
static std::string decode_iptc_text(const std::string_view sv)
{
	if (sv.empty() || str::is_utf8(sv.data(), static_cast<int>(sv.size())))
	{
		return std::string(sv);
	}

	std::string result;
	result.reserve(sv.size() + sv.size() / 2);

	for (const auto ch : sv)
	{
		const auto c = static_cast<uint8_t>(ch);

		if (c < 0x80)
		{
			result.push_back(static_cast<char>(c));
		}
		else
		{
			result.push_back(static_cast<char>(0xC0 | c >> 6));
			result.push_back(static_cast<char>(0x80 | (c & 0x3F)));
		}
	}

	return result;
}

static str::cached iptc_string(const std::string_view sv)
{
	return str::strip_and_cache(decode_iptc_text(sv));
}

// IPTC uses CR (0x0D) for line endings, normalize to LF (0x0A) for consistency
static str::cached normalize_iptc_and_cache(const std::string_view sv)
{
	auto result = decode_iptc_text(sv);
	for (auto& ch : result)
	{
		if (ch == u8'\r') ch = u8'\n';
	}
	return str::cache(str::strip(result));
}

// IPTC is parsed after Exif, so an empty dataset must not erase a value the Exif block supplied.
static void assign_if_set(str::cached& dst, const str::cached val)
{
	if (!str::is_empty(val)) dst = val;
}

static const iptc_tag_info* find_tag_info(const iptc_record r, const iptc_tag t)
{
	for (const auto* p = iptc_tag_table; p->record != 0; ++p)
	{
		if (r == p->record && t == p->tag) return p;
	}

	return nullptr;
}

// Walks the datasets in an IPTC binary block, bounds-checking every header, and calls
// handler(record, dataset, bytes, length) for each non-empty payload. Lengths are attacker
// controlled, so a malformed or over-long header stops the walk rather than being trusted.
template <typename Handler>
static void walk_iptc_datasets(const df::cspan cs, Handler handler)
{
	uint32_t i = 0;

	// Find the beginning of the IPTC portion of the binary data.
	while (i + 1 < cs.size && (cs.data[i] != 0x1c || cs.data[i + 1] != 0x02))
	{
		i += 1;
	}

	while (i < cs.size)
	{
		if (cs.data[i] != 0x1c)
		{
			break;
		}

		// Check bounds before accessing cs.data[i + 3]
		if (i + 4 >= cs.size)
		{
			break;
		}

		uint32_t block_len = 0;
		uint32_t header_len = 0;

		if (cs.data[i + 3] & static_cast<uint8_t>(0x80))
		{
			// Extended length - need at least 8 bytes total
			if (i + 7 >= cs.size)
			{
				break;
			}

			block_len = static_cast<long>(cs.data[i + 4]) << 24 |
				static_cast<long>(cs.data[i + 5]) << 16 |
				static_cast<long>(cs.data[i + 6]) << 8 |
				static_cast<long>(cs.data[i + 7]);

			header_len = 8;
		}
		else
		{
			block_len = cs.data[i + 3] << 8;
			block_len |= cs.data[i + 4];
			header_len = 5;
		}

		// Check for maximum reasonable block length to prevent memory issues
		if (block_len > 256000) // Maximum size from IPTC spec
		{
			break;
		}

		if (cs.size >= i + header_len + block_len && block_len > 0)
		{
			handler(static_cast<iptc_record>(cs.data[i + 1]), static_cast<iptc_tag>(cs.data[i + 2]),
			        cs.data + i + header_len, block_len);
		}

		// Check for potential overflow before incrementing
		if (i > cs.size - block_len - header_len)
		{
			break;
		}

		i += block_len + header_len;
	}
}

void metadata_iptc::parse(prop::item_metadata& pd, const df::cspan cs)
{
	tag_set tags;
	tag_set artists;

	walk_iptc_datasets(cs, [&](const iptc_record record, const iptc_tag dataset, const uint8_t* bytes,
	                           const uint32_t block_len)
	{
		if (record != IPTC_RECORD_APP_2) return;

		const auto sv = std::string_view{std::bit_cast<const char*>(bytes), block_len};

		switch (dataset)
		{
		case IPTC_TAG_KEYWORDS: tags.add_one(iptc_string(sv));
			break;
		case IPTC_TAG_BYLINE: artists.add_one(iptc_string(sv));
			break;
		case IPTC_TAG_CAPTION: assign_if_set(pd.description, normalize_iptc_and_cache(sv));
			break;
		case IPTC_TAG_OBJECT_NAME: assign_if_set(pd.title, iptc_string(sv));
			break;
		case IPTC_TAG_CITY: assign_if_set(pd.location_place, iptc_string(sv));
			break;
		case IPTC_TAG_STATE: assign_if_set(pd.location_state, iptc_string(sv));
			break;
		case IPTC_TAG_COUNTRY_NAME: assign_if_set(pd.location_country, iptc_string(sv));
			break;
		case IPTC_TAG_CREDIT: assign_if_set(pd.copyright_credit, iptc_string(sv));
			break;
		case IPTC_TAG_SOURCE: assign_if_set(pd.copyright_source, iptc_string(sv));
			break;
		case IPTC_TAG_COPYRIGHT_NOTICE: assign_if_set(pd.copyright_notice, iptc_string(sv));
			break;
		default:
			break;
		}
	});

	if (!tags.is_empty())
	{
		tags.add(tag_set(pd.tags));
		tags.make_unique();
		pd.tags = str::cache(tags.to_string());
	}

	if (!artists.is_empty())
	{
		artists.add(tag_set(pd.artist));
		artists.make_unique();
		pd.artist = str::cache(artists.to_string());
	}
}

static str::cached tag_name(const iptc_record r, const iptc_tag t)
{
	const auto* const info = find_tag_info(r, t);
	return info ? str::cache(info->title) : "?"_c;
}

metadata_kv_list metadata_iptc::to_info(const df::cspan cs)
{
	metadata_kv_list result;

	walk_iptc_datasets(cs, [&result](const iptc_record record, const iptc_tag tag, const uint8_t* bytes,
	                                 const uint32_t block_len)
	{
		const auto* const info = find_tag_info(record, tag);

		// Preview images and the coded character set escape are binary; rendering them as
		// text fills the properties list with unreadable bytes.
		if (info && info->format == IPTC_FORMAT_BINARY) return;

		std::string value;

		if (info && (info->format == IPTC_FORMAT_BYTE || info->format == IPTC_FORMAT_SHORT ||
			info->format == IPTC_FORMAT_LONG))
		{
			uint64_t n = 0;
			for (auto b = 0u; b < block_len && b < 8u; ++b) n = n << 8 | bytes[b];
			value = std::to_string(n);
		}
		else
		{
			value = str::strip(decode_iptc_text({std::bit_cast<const char*>(bytes), block_len}));
		}

		if (!value.empty())
		{
			result.emplace_back(tag_name(record, tag), std::move(value));
		}
	});

	return result;
}
