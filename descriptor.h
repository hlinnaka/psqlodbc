/* File:			descriptor.h
 *
 * Description:		This file contains defines and declarations that are related to
 *					the entire driver.
 *
 * Comments:		See "notice.txt" for copyright and license information.
 *
 * $Id: descriptor.h,v 1.10 2003/07/31 01:57:50 hinoue Exp $
 *
 */

#ifndef __DESCRIPTOR_H__
#define __DESCRIPTOR_H__

#include "psqlodbc.h"

typedef struct
{
	COL_INFO	*col_info; /* cached SQLColumns info for this table */
	char		schema[SCHEMA_NAME_STORAGE_LEN + 1];
	char		name[TABLE_NAME_STORAGE_LEN + 1];
	char		alias[TABLE_NAME_STORAGE_LEN + 1];
	char		updatable;
} TABLE_INFO;

typedef struct
{
	TABLE_INFO *ti;		/* resolve to explicit table names */
	int			column_size; /* precision in 2.x */
	int			decimal_digits; /* scale in 2.x */
	int			display_size;
	int			length;
	int			type;
	char		nullable;
	char		func;
	char		expr;
	char		quote;
	char		dquote;
	char		numeric;
	char		updatable;
	char		dot[TABLE_NAME_STORAGE_LEN + 1];
	char		name[COLUMN_NAME_STORAGE_LEN + 1];
	char		alias[COLUMN_NAME_STORAGE_LEN + 1];
	char		*schema;
} FIELD_INFO;
Int4 FI_precision(const FIELD_INFO *);
Int4 FI_scale(const FIELD_INFO *);

struct ARDFields_
{
	StatementClass	*stmt;
#if (ODBCVER >= 0x0300)
	int		size_of_rowset; /* for ODBC3 fetch operations */
#endif /* ODBCVER */
	int		bind_size;	/* size of each structure if using Row
							* Binding */
	UInt2		*row_operation_ptr;
	UInt4		*row_offset_ptr;
	BindInfoClass	*bookmark;
	BindInfoClass	*bindings;
	int		allocated;
	int		size_of_rowset_odbc2; /* for SQLExtendedFetch */
};

struct APDFields_
{
	StatementClass	*stmt;
	int		paramset_size;
	int		param_bind_type; /* size of each structure if using Param
						* Binding */
	UInt2			*param_operation_ptr;
	UInt4			*param_offset_ptr;
	ParameterInfoClass	*parameters;
	int			allocated;
};

struct IRDFields_
{
	StatementClass	*stmt;
	UInt4		*rowsFetched;
	UInt2		*rowStatusArray;
	UInt4		nfields;
	FIELD_INFO	**fi;
};

struct IPDFields_
{
	StatementClass	*stmt;
	UInt4		*param_processed_ptr;
	UInt2		*param_status_ptr;
	ParameterImplClass	*parameters;
	int			allocated;
};

void	InitializeARDFields(ARDFields *self);
void	InitializeAPDFields(APDFields *self);
/* void	InitializeIRDFields(IRDFields *self);
void	InitializeIPDFiedls(IPDFields *self); */
void	ARDFields_free(ARDFields *self);
void	APDFields_free(APDFields *self);
void	IRDFields_free(IRDFields *self);
void	IPDFields_free(IPDFields *self);
void	ARD_unbind_cols(ARDFields *self, BOOL freeall);
void	APD_free_params(APDFields *self, char option);
void	IPD_free_params(IPDFields *self, char option);
#if (ODBCVER >= 0x0300)
void	Desc_set_error(SQLHDESC hdesc, int errornumber, const char * errormsg);
#endif /* ODBCVER */

#endif
