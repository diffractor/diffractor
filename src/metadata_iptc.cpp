// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"
#include "metadata_iptc.h"
#include "model_tags.h"
#include "model_propery.h"

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
	std::u8string_view name;
	std::u8string_view title;
	std::u8string_view description;
	iptc_format format;
	iptc_mandatory mandatory;
	iptc_repeatable repeatable;
	uint32_t minbytes;
	uint32_t maxbytes;
};

static const iptc_tag_info iptc_tag_table[] = {
	{
		IPTC_RECORD_OBJECT_ENV, IPTC_TAG_MODEL_VERSION,
		u8"ModelVersion"sv,
		u8"Model Version"sv,
		u8"Version of IIM part 1."sv,
		IPTC_FORMAT_SHORT, IPTC_MANDATORY, IPTC_NOT_REPEATABLE, 2, 2
	},
	{
		IPTC_RECORD_OBJECT_ENV, IPTC_TAG_DESTINATION,
		u8"Destination"sv,
		u8"Destination"sv,
		u8"Routing information."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_REPEATABLE, 0, 1024
	},
	{
		IPTC_RECORD_OBJECT_ENV, IPTC_TAG_FILE_FORMAT,
		u8"FileFormat"sv,
		u8"File Format"sv,
		u8"File format of the data described by this metadata."sv,
		IPTC_FORMAT_SHORT, IPTC_MANDATORY, IPTC_NOT_REPEATABLE, 2, 2
	},
	{
		IPTC_RECORD_OBJECT_ENV, IPTC_TAG_FILE_VERSION,
		u8"FileVersion"sv,
		u8"File Version"sv,
		u8"Version of the file format."sv,
		IPTC_FORMAT_SHORT, IPTC_MANDATORY, IPTC_NOT_REPEATABLE, 2, 2
	},
	{
		IPTC_RECORD_OBJECT_ENV, IPTC_TAG_SERVICE_ID,
		u8"ServiceID"sv,
		u8"Service Identifier"sv,
		u8"Identifies the provider and product."sv,
		IPTC_FORMAT_STRING, IPTC_MANDATORY, IPTC_NOT_REPEATABLE, 0, 10
	},
	{
		IPTC_RECORD_OBJECT_ENV, IPTC_TAG_ENVELOPE_NUM,
		u8"EnvelopeNum"sv,
		u8"Envelope Number"sv,
		u8"A number unique for the date in 1:70 and the service ID in 1:30."sv,
		IPTC_FORMAT_NUMERIC_STRING, IPTC_MANDATORY, IPTC_NOT_REPEATABLE, 8, 8
	},
	{
		IPTC_RECORD_OBJECT_ENV, IPTC_TAG_PRODUCT_ID,
		u8"ProductID"sv,
		u8"Product I.D."sv,
		u8"Allows a provider to identify subsets of its overall service."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_REPEATABLE, 0, 32
	},
	{
		IPTC_RECORD_OBJECT_ENV, IPTC_TAG_ENVELOPE_PRIORITY,
		u8"EnvelopePriority"sv,
		u8"Envelope Priority"sv,
		u8"Specifies the envelope handling priority and not the editorial urgency.  '1' for most urgent, '5' for normal, and '8' for least urgent.  '9' is user-defined."sv,
		IPTC_FORMAT_NUMERIC_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 1, 1
	},
	{
		IPTC_RECORD_OBJECT_ENV, IPTC_TAG_DATE_SENT,
		u8"DateSent"sv,
		u8"Date Sent"sv,
		u8"The day the service sent the material."sv,
		IPTC_FORMAT_DATE, IPTC_MANDATORY, IPTC_NOT_REPEATABLE, 8, 8
	},
	{
		IPTC_RECORD_OBJECT_ENV, IPTC_TAG_TIME_SENT,
		u8"TimeSent"sv,
		u8"Time Sent"sv,
		u8"The time the service sent the material."sv,
		IPTC_FORMAT_TIME, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 11, 11
	},
	{
		IPTC_RECORD_OBJECT_ENV, IPTC_TAG_CHARACTER_SET,
		u8"CharacterSet"sv,
		u8"Coded Character Set"sv,
		u8"Control functions used for the announcement, invocation or designation of coded character sets."sv,
		IPTC_FORMAT_BINARY, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 32
	},
	{
		IPTC_RECORD_OBJECT_ENV, IPTC_TAG_UNO,
		u8"UNO"sv,
		u8"Unique Name of Object"sv,
		u8"An eternal, globally unique identification for the object, independent of provider and for any media form."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 14, 80
	},
	{
		IPTC_RECORD_OBJECT_ENV, IPTC_TAG_ARM_ID,
		u8"ARMID"sv,
		u8"ARM Identifier"sv,
		u8"Identifies the Abstract Relationship Method (ARM)."sv,
		IPTC_FORMAT_SHORT, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 2, 2
	},
	{
		IPTC_RECORD_OBJECT_ENV, IPTC_TAG_ARM_VERSION,
		u8"ARMVersion"sv,
		u8"ARM Version"sv,
		u8"Identifies the version of the Abstract Relationship Method (ARM)."sv,
		IPTC_FORMAT_SHORT, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 2, 2
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_RECORD_VERSION,
		u8"RecordVersion"sv,
		u8"Record Version"sv,
		u8"Identifies the version of the IIM, Part 2"sv,
		IPTC_FORMAT_SHORT, IPTC_MANDATORY, IPTC_NOT_REPEATABLE, 2, 2
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_OBJECT_TYPE,
		u8"ObjectType"sv,
		u8"Object Type Reference"sv,
		u8"Distinguishes between different types of objects within the IIM."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 3, 67
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_OBJECT_ATTRIBUTE,
		u8"ObjectAttribute"sv,
		u8"Object Attribute Reference"sv,
		u8"Defines the nature of the object independent of the subject."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_REPEATABLE, 4, 68
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_OBJECT_NAME,
		u8"ObjectName"sv,
		u8"Object Name"sv,
		u8"A shorthand reference for the object."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 64
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_EDIT_STATUS,
		u8"EditStatus"sv,
		u8"Edit Status"sv,
		u8"Status of the object, according to the practice of the provider."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 64
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_EDITORIAL_UPDATE,
		u8"EditorialUpdate"sv,
		u8"Editorial Update"sv,
		u8"Indicates the type of update this object provides to a previous object.  The link to the previous object is made using the ARM.  '01' indicates an additional language."sv,
		IPTC_FORMAT_NUMERIC_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 2, 2
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_URGENCY,
		u8"Urgency"sv,
		u8"Urgency"sv,
		u8"Specifies the editorial urgency of content and not necessarily the envelope handling priority.  '1' is most urgent, '5' normal, and '8' least urgent."sv,
		IPTC_FORMAT_NUMERIC_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 1, 1
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_SUBJECT_REFERENCE,
		u8"SubjectRef"sv,
		u8"Subject Reference"sv,
		u8"A structured definition of the subject matter.  It must contain an IPR, an 8 digit Subject Reference Number and an optional Subject Name, Subject Matter Name, and Subject Detail Name each separated by a colon (:)."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_REPEATABLE, 13, 236
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_CATEGORY,
		u8"Category"sv,
		u8"Category"sv,
		u8"Identifies the subject of the object in the opinion of the provider (Deprecated)."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 3
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_SUPPL_CATEGORY,
		u8"SupplCategory"sv,
		u8"Supplemental Category"sv,
		u8"Further refines the subject of the object (Deprecated)."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_REPEATABLE, 0, 32
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_FIXTURE_ID,
		u8"FixtureID"sv,
		u8"Fixture Identifier"sv,
		u8"Identifies objects that recur often and predictably, enabling users to immediately find or recall such an object."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 32
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_KEYWORDS,
		u8"Keywords"sv,
		u8"Keywords"sv,
		u8"Used to indicate specific information retrieval words."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_REPEATABLE, 0, 64
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_CONTENT_LOC_CODE,
		u8"ContentLocCode"sv,
		u8"Content Location Code"sv,
		u8"Indicates the code of a country/geographical location referenced by the content of the object."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_REPEATABLE, 3, 3
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_CONTENT_LOC_NAME,
		u8"ContentLocName"sv,
		u8"Content Location Name"sv,
		u8"A full, publishable name of a country/geographical location referenced by the content of the object."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_REPEATABLE, 0, 64
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_RELEASE_DATE,
		u8"ReleaseDate"sv,
		u8"Release Date"sv,
		u8"Designates the earliest date the provider intends the object to be used."sv,
		IPTC_FORMAT_DATE, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 8, 8
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_RELEASE_TIME,
		u8"ReleaseTime"sv,
		u8"Release Time"sv,
		u8"Designates the earliest time the provider intends the object to be used."sv,
		IPTC_FORMAT_TIME, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 11, 11
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_EXPIRATION_DATE,
		u8"ExpirationDate"sv,
		u8"Expiration Date"sv,
		u8"Designates the latest date the provider intends the object to be used."sv,
		IPTC_FORMAT_DATE, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 8, 8
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_EXPIRATION_TIME,
		u8"ExpirationTime"sv,
		u8"Expiration Time"sv,
		u8"Designates the latest time the provider intends the object to be used."sv,
		IPTC_FORMAT_TIME, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 11, 11
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_SPECIAL_INSTRUCTIONS,
		u8"SpecialInstructions"sv,
		u8"Special Instructions"sv,
		u8"Other editorial instructions concerning the use of the object."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 256
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_ACTION_ADVISED,
		u8"ActionAdvised"sv,
		u8"Action Advised"sv,
		u8"The type of action that this object provides to a previous object.  '01' Object Kill, '02' Object Replace, '03' Object Append, '04' Object Reference."sv,
		IPTC_FORMAT_NUMERIC_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 2, 2
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_REFERENCE_SERVICE,
		u8"RefService"sv,
		u8"Reference Service"sv,
		u8"The Service Identifier of a prior envelope to which the current object refers."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_REPEATABLE, 0, 10
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_REFERENCE_DATE,
		u8"RefDate"sv,
		u8"Reference Date"sv,
		u8"The date of a prior envelope to which the current object refers."sv,
		IPTC_FORMAT_DATE, IPTC_OPTIONAL, IPTC_REPEATABLE, 8, 8
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_REFERENCE_NUMBER,
		u8"RefNumber"sv,
		u8"Reference Number"sv,
		u8"The Envelope Number of a prior envelope to which the current object refers."sv,
		IPTC_FORMAT_NUMERIC_STRING, IPTC_OPTIONAL, IPTC_REPEATABLE, 8, 8
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_DATE_CREATED,
		u8"DateCreated"sv,
		u8"Date Created"sv,
		u8"The date the intellectual content of the object was created rather than the date of the creation of the physical representation."sv,
		IPTC_FORMAT_DATE, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 8, 8
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_TIME_CREATED,
		u8"TimeCreated"sv,
		u8"Time Created"sv,
		u8"The time the intellectual content of the object was created rather than the date of the creation of the physical representation."sv,
		IPTC_FORMAT_TIME, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 11, 11
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_DIGITAL_CREATION_DATE,
		u8"DigitalCreationDate"sv,
		u8"Digital Creation Date"sv,
		u8"The date the digital representation of the object was created."sv,
		IPTC_FORMAT_DATE, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 8, 8
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_DIGITAL_CREATION_TIME,
		u8"DigitalCreationTime"sv,
		u8"Digital Creation Time"sv,
		u8"The time the digital representation of the object was created."sv,
		IPTC_FORMAT_TIME, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 11, 11
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_ORIGINATING_PROGRAM,
		u8"OriginatingProgram"sv,
		u8"Originating Program"sv,
		u8"The type of program used to originate the object."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 32
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_PROGRAM_VERSION,
		u8"ProgramVersion"sv,
		u8"Program Version"sv,
		u8"The version of the originating program."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 10
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_OBJECT_CYCLE,
		u8"ObjectCycle"sv,
		u8"Object Cycle"sv,
		u8"Where 'a' is morning, 'b' is evening, 'b' is both."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 1, 1
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_BYLINE,
		u8"Byline"sv,
		u8"By-line"sv,
		u8"Name of the creator of the object, e.g. writer, photographer or graphic artist."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_REPEATABLE, 0, 32
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_BYLINE_TITLE,
		u8"BylineTitle"sv,
		u8"By-line Title"sv,
		u8"Title of the creator or creators of the object."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_REPEATABLE, 0, 32
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_CITY,
		u8"City"sv,
		u8"City"sv,
		u8"City of object origin."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 32
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_SUBLOCATION,
		u8"Sublocation"sv,
		u8"Sub-location"sv,
		u8"The location within a city from which the object originates."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 32
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_STATE,
		u8"State"sv,
		u8"Province/State"sv,
		u8"The Province/State where the object originates."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 32
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_COUNTRY_CODE,
		u8"CountryCode"sv,
		u8"Country Code"sv,
		u8"The code of the country/primary location where the object was created."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 3, 3
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_COUNTRY_NAME,
		u8"CountryName"sv,
		u8"Country Name"sv,
		u8"The name of the country/primary location where the object was created."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 64
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_ORIG_TRANS_REF,
		u8"OrigTransRef"sv,
		u8"Original Transmission Reference"sv,
		u8"A code representing the location of original transmission."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 32
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_HEADLINE,
		u8"Headline"sv,
		u8"Headline"sv,
		u8"A publishable entry providing a synopsis of the contents of the object."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 256
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_CREDIT,
		u8"Credit"sv,
		u8"Credit"sv,
		u8"Identifies the provider of the object, not necessarily the owner/creator."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 32
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_SOURCE,
		u8"Source"sv,
		u8"Source"sv,
		u8"The original owner of the intellectual content of the object."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 32
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_COPYRIGHT_NOTICE,
		u8"CopyrightNotice"sv,
		u8"Copyright Notice"sv,
		u8"Any necessary copyright notice."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 128
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_CONTACT,
		u8"Contact"sv,
		u8"Contact"sv,
		u8"The person or organization which can provide further background information on the object."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_REPEATABLE, 0, 128
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_CAPTION,
		u8"Caption"sv,
		u8"Caption/Abstract"sv,
		u8"A textual description of the data"sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 2000
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_WRITER_EDITOR,
		u8"WriterEditor"sv,
		u8"Writer/Editor"sv,
		u8"The name of the person involved in the writing, editing or correcting the object or caption/abstract"sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_REPEATABLE, 0, 32
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_RASTERIZED_CAPTION,
		u8"RasterizedCaption"sv,
		u8"Rasterized Caption"sv,
		u8"Contains rasterized object description and is used where characters that have not been coded are required for the caption."sv,
		IPTC_FORMAT_BINARY, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 7360, 7360
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_IMAGE_TYPE,
		u8"ImageType"sv,
		u8"Image Type"sv,
		u8"Indicates the data format of the image object."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 2, 2
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_IMAGE_ORIENTATION,
		u8"ImageOrientation"sv,
		u8"Image Orientation"sv,
		u8"The layout of the image area: 'P' for portrait, 'L' for landscape, and 'S' for square."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 1, 1
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_LANGUAGE_ID,
		u8"LanguageID"sv,
		u8"Language Identifier"sv,
		u8"The major national language of the object, according to the 2-letter codes of ISO 639:1988."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 2, 3
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_AUDIO_TYPE,
		u8"AudioType"sv,
		u8"Audio Type"sv,
		u8"The number of channels and type of audio (music, text, etc.) in the object."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 2, 2
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_AUDIO_SAMPLING_RATE,
		u8"AudioSamplingRate"sv,
		u8"Audio Sampling Rate"sv,
		u8"The sampling rate in Hz of the audio data."sv,
		IPTC_FORMAT_NUMERIC_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 6, 6
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_AUDIO_SAMPLING_RES,
		u8"AudioSamplingRes"sv,
		u8"Audio Sampling Resolution"sv,
		u8"The number of bits in each audio sample."sv,
		IPTC_FORMAT_NUMERIC_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 2, 2
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_AUDIO_DURATION,
		u8"AudioDuration"sv,
		u8"Audio Duration"sv,
		u8"The running time of the audio data in the form HHMMSS."sv,
		IPTC_FORMAT_NUMERIC_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 6, 6
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_AUDIO_OUTCUE,
		u8"AudioOutcue"sv,
		u8"Audio Outcue"sv,
		u8"The content at the end of the audio data."sv,
		IPTC_FORMAT_STRING, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 64
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_PREVIEW_FORMAT,
		u8"PreviewFileFormat"sv,
		u8"Preview File Format"sv,
		u8"Binary value indicating the file format of the object preview data in dataset 2:202."sv,
		IPTC_FORMAT_SHORT, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 2, 2
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_PREVIEW_FORMAT_VER,
		u8"PreviewFileFormatVer"sv,
		u8"Preview File Format Version"sv,
		u8"The version of the preview file format specified in 2:200."sv,
		IPTC_FORMAT_SHORT, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 2, 2
	},
	{
		IPTC_RECORD_APP_2, IPTC_TAG_PREVIEW_DATA,
		u8"PreviewData"sv,
		u8"Preview Data"sv,
		u8"The object preview data"sv,
		IPTC_FORMAT_BINARY, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 0, 256000
	},
	{
		IPTC_RECORD_PREOBJ_DATA, IPTC_TAG_SIZE_MODE,
		u8"SizeMode"sv,
		u8"Size Mode"sv,
		u8"Set to 0 if the size of the object is known and 1 if not known."sv,
		IPTC_FORMAT_BYTE, IPTC_MANDATORY, IPTC_NOT_REPEATABLE, 1, 1
	},
	{
		IPTC_RECORD_PREOBJ_DATA, IPTC_TAG_MAX_SUBFILE_SIZE,
		u8"MaxSubfileSize"sv,
		u8"Max Subfile Size"sv,
		u8"The maximum size for a subfile dataset (8:10) containing a portion of the object data."sv,
		IPTC_FORMAT_LONG, IPTC_MANDATORY, IPTC_NOT_REPEATABLE, 4, 4
	},
	{
		IPTC_RECORD_PREOBJ_DATA, IPTC_TAG_SIZE_ANNOUNCED,
		u8"ObjectSizeAnnounced"sv,
		u8"Object Size Announced"sv,
		u8"The total size of the object data if it is known."sv,
		IPTC_FORMAT_LONG, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 4, 4
	},
	{
		IPTC_RECORD_PREOBJ_DATA, IPTC_TAG_MAX_OBJECT_SIZE,
		u8"MaxObjectSize"sv,
		u8"Maximum Object Size"sv,
		u8"The largest possible size of the object if the size is not known."sv,
		IPTC_FORMAT_LONG, IPTC_OPTIONAL, IPTC_NOT_REPEATABLE, 4, 4
	},
	{
		IPTC_RECORD_OBJ_DATA, IPTC_TAG_SUBFILE,
		u8"Subfile"sv,
		u8"Subfile"sv,
		u8"The object data itself.  Subfiles must be sequential so that the subfiles may be reassembled."sv,
		IPTC_FORMAT_BINARY, IPTC_MANDATORY, IPTC_REPEATABLE, 0, UINT32_MAX
	},
	{
		IPTC_RECORD_POSTOBJ_DATA, IPTC_TAG_CONFIRMED_DATA_SIZE,
		u8"ConfirmedDataSize"sv,
		u8"Confirmed Data Size"sv,
		u8"Total size of the object data."sv,
		IPTC_FORMAT_LONG, IPTC_MANDATORY, IPTC_NOT_REPEATABLE, 4, 4
	},
	{static_cast<iptc_record>(0), static_cast<iptc_tag>(0), {}, {}, {}}
};

void metadata_iptc::parse(prop::item_metadata& pd, df::cspan cs)
{
	if (!cs.empty())
	{
		uint32_t block_len = 0;
		uint32_t header_len = 0;
		uint32_t i = 0;
		tag_set tags;
		tag_set artists;

		// Find the beginning of the IPTC portion of the binary data.
		while ((cs.data[i] != 0x1c || cs.data[i + 1] != 0x02) && i < cs.size)
		{
			i += 1;
		}

		while ((i < cs.size) && (i >= 0))
		{
			if (cs.data[i] != 0x1c)
			{
				break;
			}
			if (cs.data[i + 3] & static_cast<uint8_t>(0x80))
			{
				block_len = (static_cast<long>(cs.data[i + 4]) << 24) |
					(static_cast<long>(cs.data[i + 5]) << 16) |
					(static_cast<long>(cs.data[i + 6]) << 8) |
					(static_cast<long>(cs.data[i + 7]));

				header_len = 8;
			}
			else
			{
				block_len = cs.data[i + 3] << 8;
				block_len |= cs.data[i + 4];
				header_len = 5;
			}

			if (block_len < 0)
			{
				break;
			}

			const auto dataset = cs.data[i + 1];
			const auto record = static_cast<iptc_tag>(cs.data[i + 2]);

			if (cs.size >= (i + block_len) && block_len > 0 && dataset == 2)
			{
				const auto* const sz = std::bit_cast<const char8_t*>(cs.data + i + header_len);
				const auto sv = std::u8string_view{ sz, block_len };

				switch (record)
				{
				case IPTC_TAG_KEYWORDS: tags.add_one(str::strip(sv));
					break;
				case IPTC_TAG_BYLINE: artists.add_one(str::strip_and_cache(sv));
					break;
				case IPTC_TAG_CAPTION: pd.description = str::strip_and_cache(sv);
					break;
				case IPTC_TAG_OBJECT_NAME: pd.title = str::strip_and_cache(sv);
					break;
				case IPTC_TAG_CITY: pd.location_place = str::strip_and_cache(sv);
					break;
				case IPTC_TAG_STATE: pd.location_state = str::strip_and_cache(sv);
					break;
				case IPTC_TAG_COUNTRY_NAME: pd.location_country = str::strip_and_cache(sv);
					break;
				case IPTC_TAG_CREDIT: pd.copyright_credit = str::strip_and_cache(sv);
					break;
				case IPTC_TAG_SOURCE: pd.copyright_source = str::strip_and_cache(sv);
					break;
				case IPTC_TAG_COPYRIGHT_NOTICE: pd.copyright_notice = str::strip_and_cache(sv);
					break;
				default:
					break;
				}
			}

			i += block_len + header_len;
		}

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
}

static str::cached tag_name(const iptc_record r, const iptc_tag t)
{
	const auto* p = iptc_tag_table;

	while (p->record != 0)
	{
		if (r == p->record && t == p->tag) return str::cache(p->title);
		++p;
	}

	return u8"?"_c;
}

metadata_kv_list metadata_iptc::to_info(df::cspan cs)
{
	metadata_kv_list result;

	if (!cs.empty())
	{
		uint32_t block_len = 0;
		uint32_t header_len = 0;
		uint32_t i = 0;

		// Find the beginning of the IPTC portion of the binary data.
		while ((cs.data[i] != 0x1c || cs.data[i + 1] != 0x02) && i < cs.size)
		{
			i += 1;
		}

		while ((i < cs.size) && (i >= 0))
		{
			if (cs.data[i] != 0x1c)
			{
				break;
			}
			if (cs.data[i + 3] & static_cast<uint8_t>(0x80))
			{
				block_len = (static_cast<long>(cs.data[i + 4]) << 24) |
					(static_cast<long>(cs.data[i + 5]) << 16) |
					(static_cast<long>(cs.data[i + 6]) << 8) |
					(static_cast<long>(cs.data[i + 7]));

				header_len = 8;
			}
			else
			{
				block_len = cs.data[i + 3] << 8;
				block_len |= cs.data[i + 4];
				header_len = 5;
			}

			if (block_len < 0)
			{
				break;
			}

			const auto record = static_cast<iptc_record>(cs.data[i + 1]);
			const auto tag = static_cast<iptc_tag>(cs.data[i + 2]);

			if (cs.size >= (i + block_len) && block_len > 0)
			{
				const auto* const sz = std::bit_cast<const char8_t*>(cs.data + i + header_len);
				result.emplace_back(tag_name(record, tag), str::strip({ sz, block_len }));
			}

			i += block_len + header_len;
		}
	}

	return result;
}
