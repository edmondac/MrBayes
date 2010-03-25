/*
 *  MrBayes 3.1.2
 *
 *  copyright 2002-2005
 *
 *  John P. Huelsenbeck
 *  Section of Ecology, Behavior and Evolution
 *  Division of Biological Sciences
 *  University of California, San Diego
 *  La Jolla, CA 92093-0116
 *
 *  johnh@biomail.ucsd.edu
 *
 *	Fredrik Ronquist
 *  Paul van der Mark
 *  School of Computational Science
 *  Florida State University
 *  Tallahassee, FL 32306-4120
 *
 *  ronquist@scs.fsu.edu
 *  paulvdm@scs.fsu.edu
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details (www.gnu.org).
 *
 */
/* id-string for ident, do not edit: cvs will update this string */
const char sumpID[]="$Id: sump.c,v 3.37 2009/01/06 21:40:10 ronquist Exp $";

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <string.h>
#include <ctype.h>
#include "mb.h"
#include "globals.h"
#include "command.h"
#include "bayes.h"
#include "sump.h"
#include "mcmc.h"
#include "utils.h"
#if defined(__MWERKS__)
#include "SIOUX.h"
#endif

/* local prototypes */
int		 PrintModelStats (char *fileName, char **headerNames, int nHeaders, ParameterSample *parameterSamples, int nRuns, int nSamples);
int		 PrintOverlayPlot (MrBFlt **xVals, MrBFlt **yVals, int nRows, int nSamples);
int		 PrintParamStats (char *fileName, char **headerNames, int nHeaders, ParameterSample *parameterSamples, int nRuns, int nSamples);
void	 PrintPlotHeader (void);



/* AllocateParameterSamples: Allocate space for parameter samples */
int AllocateParameterSamples (ParameterSample **parameterSamples, int numRuns, int numRows, int numColumns)
{
    int     i, j;
    
    (*parameterSamples) = (ParameterSample *) SafeCalloc (numColumns, sizeof(ParameterSample));
    if (!(*parameterSamples))
        return ERROR;
    (*parameterSamples)[0].values = (MrBFlt **) SafeCalloc (numColumns * numRuns, sizeof (MrBFlt *));
    if (!((*parameterSamples)[0].values))
        {
        FreeParameterSamples(*parameterSamples);
        return ERROR;
        }
    (*parameterSamples)[0].values[0] = (MrBFlt *) SafeCalloc (numColumns * numRuns * numRows, sizeof (MrBFlt));
    for (i=1; i<numColumns; i++)
        (*parameterSamples)[i].values = (*parameterSamples)[0].values + i*numRuns;
    for (i=1; i<numRuns; i++)
        (*parameterSamples)[0].values[i] = (*parameterSamples)[0].values[0] + i*numRows;
    for (i=1; i<numColumns; i++)
        {
        for (j=0; j<numRuns; j++)
            {
            (*parameterSamples)[i].values[j] = (*parameterSamples)[0].values[0] + i*numRuns*numRows + j*numRows;
            }
        }

    return NO_ERROR;
}





int DoSump (void)

{

	int			    i, n, nHeaders=0, numRows, numColumns, numRuns, whichIsX, whichIsY,
				    unreliable, oneUnreliable, burnin, longestHeader, len;
	MrBFlt		    mean, harm_mean;
	char		    **headerNames=NULL, temp[100];
    SumpFileInfo    fileInfo, firstFileInfo;
    ParameterSample *parameterSamples=NULL;

#	if defined (MPI_ENABLED)
    if (proc_id != 0)
		return NO_ERROR;
#	endif

	/* tell user we are ready to go */
	if (sumpParams.numRuns == 1)
		MrBayesPrint ("%s   Summarizing parameters in file %s.p\n", spacer, sumpParams.sumpFileName);
	else if (sumpParams.numRuns == 2)
		MrBayesPrint ("%s   Summarizing parameters in files %s.run1.p and %s.run2.p\n", spacer, sumpParams.sumpFileName, sumpParams.sumpFileName);
	else /* if (sumpParams.numRuns > 2) */
		{
		MrBayesPrint ("%s   Summarizing parameters in %d files (%s.run1.p,\n", spacer, sumpParams.numRuns, sumpParams.sumpFileName);
		MrBayesPrint ("%s      %s.run2.p, etc)\n", spacer, sumpParams.sumpFileName);
		}
	MrBayesPrint ("%s   Writing summary statistics to file %s.pstat\n", spacer, sumpParams.sumpFileName);

	/* examine input file(s) */
    for (i=0; i<sumpParams.numRuns; i++)
        {
        if (sumtParams.numRuns == 1)
            sprintf (temp, "%s.p", sumpParams.sumpFileName);
        else
            sprintf (temp, "%s.run%d.p", sumpParams.sumpFileName, i+1);

        if (ExamineSumpFile (temp, &fileInfo, &headerNames, &nHeaders) == ERROR)
	        goto errorExit;

        if (i==0)
            {
        	if (fileInfo.numRows == 0 || fileInfo.numColumns == 0)
				{
				MrBayesPrint ("%s   The number of rows or columns in file %d is equal to zero\n", spacer, temp);
				goto errorExit;
				}
            firstFileInfo = fileInfo;
            }
        else
            {
            if (firstFileInfo.numRows != fileInfo.numRows || firstFileInfo.numColumns != fileInfo.numColumns)
                {
                MrBayesPrint ("%s   First file had %d rows and %d columns while file %s had %d rows and %d columns\n",
                    spacer, firstFileInfo.numRows, firstFileInfo.numColumns, temp, fileInfo.numRows, fileInfo.numColumns);
                MrBayesPrint ("%s   MrBayes expects the same number of rows and columns in all files\n", spacer);
                goto errorExit;
                }
            }
        }

    numRows = fileInfo.numRows;
    numColumns = fileInfo.numColumns;
    numRuns = sumpParams.numRuns;

    /* allocate space to hold parameter information */
    if (AllocateParameterSamples (&parameterSamples, numRuns, numRows, numColumns) == ERROR)
        return ERROR;

    /* read samples */
    for (i=0; i<sumpParams.numRuns; i++)
        {
        /* derive file name */
        if (sumtParams.numRuns == 1)
            sprintf (temp, "%s.p", sumpParams.sumpFileName);
        else
            sprintf (temp, "%s.run%d.p", sumpParams.sumpFileName, i+1);
        
        /* read samples */    
        if (ReadParamSamples (temp, &fileInfo, parameterSamples, i) == ERROR)
            goto errorExit;
        }

	/* get length of longest header */
	longestHeader = 9; /* 9 is the length of the word "parameter" (for printing table) */
	for (i=0; i<nHeaders; i++)
		{
		len = (int) strlen(headerNames[i]);
		if (len > longestHeader)
			longestHeader = len;
		}
			
	/* Print header */
	PrintPlotHeader ();

    /* Print trace plots */
    if (FindHeader("Gen", headerNames, nHeaders, &whichIsX) == ERROR)
        {
		MrBayesPrint ("%s   Could not find the 'Gen' column\n", spacer);
        return ERROR;
        }
    if (FindHeader("LnL", headerNames, nHeaders, &whichIsY) == ERROR)
        {
		MrBayesPrint ("%s   Could not find the 'LnL' column\n", spacer);
        return ERROR;
        }
    if (sumpParams.numRuns > 1)
		{
		if (sumpParams.allRuns == YES)
			{
			for (i=0; i<sumpParams.numRuns; i++)
				{
				MrBayesPrint ("\n%s   Samples from run %d:\n", spacer, i+1);
				if (PrintPlot (parameterSamples[whichIsX].values[i], parameterSamples[whichIsY].values[i], numRows) == ERROR)
					goto errorExit;
				}
			}
		else
            {
            if (PrintOverlayPlot (parameterSamples[whichIsX].values, parameterSamples[whichIsY].values, numRuns, numRows) == ERROR)
			    goto errorExit;
            }
		}
	else
		{
		if (PrintPlot (parameterSamples[whichIsX].values[0], parameterSamples[whichIsY].values[0], numRows) == ERROR)
			goto errorExit;
        }
			
	/* calculate arithmetic and harmonic means of likelihoods */
	oneUnreliable = NO;
	for (n=0; n<sumpParams.numRuns; n++)
		{
		unreliable = NO;
		if (HarmonicArithmeticMeanOnLogs (parameterSamples[whichIsY].values[n], numRows, &mean, &harm_mean) == ERROR)
			{
			unreliable = YES;
			oneUnreliable = YES;
			}
		if (sumpParams.numRuns == 1)
			{
			MrBayesPrint ("\n");
			MrBayesPrint ("%s   Estimated marginal likelihoods for run sampled in file \"%s.p\":\n", spacer, sumpParams.sumpFileName);
			MrBayesPrint ("%s      (Use the harmonic mean for Bayes factor comparisons of models)\n\n", spacer, sumpParams.sumpFileName);
			MrBayesPrint ("%s   Arithmetic mean   Harmonic mean\n", spacer);
			MrBayesPrint ("%s   --------------------------------\n", spacer);
			if (unreliable == NO)
				MrBayesPrint ("%s     %9.2lf        %9.2lf\n", spacer, mean, harm_mean);
			else
				MrBayesPrint ("%s     %9.2lf *      %9.2lf *\n", spacer, mean, harm_mean);
			}
		else
			{
			if (n == 0)
				{
				MrBayesPrint ("\n");
				MrBayesPrint ("%s   Estimated marginal likelihoods for runs sampled in files\n", spacer);
				if (sumpParams.numRuns > 2)
					MrBayesPrint ("%s      \"%s.run1.p\", \"%s.run2.p\", etc:\n", spacer, sumpParams.sumpFileName, sumpParams.sumpFileName);
				else /* if (sumpParams.numRuns == 2) */
					MrBayesPrint ("%s      \"%s.run1.p\" and \"%s.run2.p\":\n", spacer, sumpParams.sumpFileName, sumpParams.sumpFileName);
			    MrBayesPrint ("%s      (Use the harmonic mean for Bayes factor comparisons of models)\n\n", spacer, sumpParams.sumpFileName);
				MrBayesPrint ("%s   Run   Arithmetic mean   Harmonic mean\n", spacer);
				MrBayesPrint ("%s   --------------------------------------\n", spacer);
				}
			if (unreliable == NO)
				MrBayesPrint ("%s   %3d     %9.2lf        %9.2lf\n", spacer, n+1, mean, harm_mean);
			else
				MrBayesPrint ("%s   %3d     %9.2lf *      %9.2lf *\n", spacer, n+1, mean, harm_mean);
			}					
		}	/* next run */
	if (sumpParams.numRuns == 1)
		{
		MrBayesPrint ("%s   --------------------------------\n", spacer);
		}
	else
		{
		if (HarmonicArithmeticMeanOnLogs (parameterSamples[whichIsY].values[0], sumpParams.numRuns*numRows, &mean, &harm_mean) == ERROR)
			{
			unreliable = YES;
			oneUnreliable = YES;
			}
		else
			unreliable = NO;
		MrBayesPrint ("%s   --------------------------------------\n", spacer);
		if (unreliable == YES)
			MrBayesPrint ("%s   TOTAL   %9.2lf *      %9.2lf *\n", spacer, mean, harm_mean);
		else
			MrBayesPrint ("%s   TOTAL   %9.2lf        %9.2lf\n", spacer, mean, harm_mean);
		MrBayesPrint ("%s   --------------------------------------\n", spacer);
		}
	if (oneUnreliable == YES)
		{
		MrBayesPrint ("%s   * These estimates may be unreliable because \n", spacer);
		MrBayesPrint ("%s     some extreme values were excluded\n\n", spacer);
		}
	else
		{
		MrBayesPrint ("\n");
		}

    /* Calculate burnin */
    burnin = fileInfo.firstParamLine - fileInfo.headerLine;

    /* Print parameter information to screen and to file. */
	if (sumpParams.numRuns > 1 && sumpParams.allRuns == YES)
		{
		for (i=0; i<sumpParams.numRuns; i++)
			{
			/* print table header */
			MrBayesPrint ("\n");
			MrBayesPrint ("%s   Model parameter summaries for run sampled in file \"%s.run%d.p\":\n", spacer, sumpParams.sumpFileName, i+1);
			MrBayesPrint ("%s      (Based on %d samples out of a total of %d samples from this analysis)\n\n", spacer, numRows, numRows + burnin);
            if (PrintParamStats (sumpParams.sumpOutfile, headerNames, nHeaders, parameterSamples, numRuns, numRows) == ERROR)
				goto errorExit;
			if (PrintModelStats (sumpParams.sumpOutfile, headerNames, nHeaders, parameterSamples, numRuns, numRows) == ERROR)
				goto errorExit;
			}
        }

	MrBayesPrint ("\n");
	if (sumpParams.numRuns == 1)
		MrBayesPrint ("%s   Model parameter summaries for run sampled in file \"%s\":\n", spacer, sumpParams.sumpFileName);
	else if (sumpParams.numRuns == 2)
		{
		MrBayesPrint ("%s   Model parameter summaries over the runs sampled in files\n", spacer);
		MrBayesPrint ("%s      \"%s.run1.p\" and \"%s.run2.p\":\n", spacer, sumpParams.sumpFileName, sumpParams.sumpFileName);
		}
	else
		{
		MrBayesPrint ("%s   Model parameter summaries over all %d runs sampled in files\n", spacer, sumpParams.numRuns);
		MrBayesPrint ("%s      \"%s.run1.p\", \"%s.run2.p\" etc:\n", spacer, sumpParams.sumpFileName, sumpParams.sumpFileName);
		}
	if (sumpParams.numRuns == 1)
		MrBayesPrint ("%s      Based on a total of %d samples out of a total of %d samples from this analysis.\n\n", spacer, numRows, numRows + burnin);
	else
		{
		MrBayesPrint ("%s      Summaries are based on a total of %d samples from %d runs.\n", spacer, sumpParams.numRuns*numRows, sumpParams.numRuns);
		MrBayesPrint ("%s      Each run produced %d samples of which %d samples were included.\n", spacer, numRows + burnin, numRows);
		}
	MrBayesPrint ("%s      Parameter summaries are saved to file \"%s.pstat\".\n", spacer, sumpParams.sumpOutfile);

    if (PrintParamStats (sumpParams.sumpFileName, headerNames, nHeaders, parameterSamples, numRuns, numRows) == ERROR)
		goto errorExit;
    if (PrintModelStats (sumpParams.sumpFileName, headerNames, nHeaders, parameterSamples, numRuns, numRows) == ERROR)
		goto errorExit;

    /* free memory */
    FreeParameterSamples(parameterSamples);
    for (i=0; i<nHeaders; i++)
        free (headerNames[i]);
    free (headerNames);

    expecting = Expecting(COMMAND);
	strcpy (spacer, "");
	
	return (NO_ERROR);
	
errorExit:

    /* free memory */
    FreeParameterSamples (parameterSamples);
    for (i=0; i<nHeaders; i++)
        free (headerNames[i]);
    free (headerNames);

    expecting = Expecting(COMMAND);    
	strcpy (spacer, "");

	return (ERROR);
}





int DoSumpParm (char *parmName, char *tkn)

{

	int			tempI;
    MrBFlt      tempD;
	char		tempStr[100];

	if (expecting == Expecting(PARAMETER))
		{
		expecting = Expecting(EQUALSIGN);
		}
	else
		{
		if (!strcmp(parmName, "Xxxxxxxxxx"))
			{
			expecting  = Expecting(PARAMETER);
			expecting |= Expecting(SEMICOLON);
			}
		/* set Filename (sumpParams.sumpFileName) ***************************************************/
		else if (!strcmp(parmName, "Filename"))
			{
			if (expecting == Expecting(EQUALSIGN))
				{
				expecting = Expecting(ALPHA);
				readWord = YES;
				}
			else if (expecting == Expecting(ALPHA))
				{
				strcpy (sumpParams.sumpFileName, tkn);
				MrBayesPrint ("%s   Setting sump filename to %s\n", spacer, sumpParams.sumpFileName);
				expecting = Expecting(PARAMETER) | Expecting(SEMICOLON);
				}
			else
				return (ERROR);
			}
		/* set Relburnin (sumpParams.relativeBurnin) ********************************************************/
		else if (!strcmp(parmName, "Relburnin"))
			{
			if (expecting == Expecting(EQUALSIGN))
				expecting = Expecting(ALPHA);
			else if (expecting == Expecting(ALPHA))
				{
				if (IsArgValid(tkn, tempStr) == NO_ERROR)
					{
					if (!strcmp(tempStr, "Yes"))
						sumpParams.relativeBurnin = YES;
					else
						sumpParams.relativeBurnin = NO;
					}
				else
					{
					MrBayesPrint ("%s   Invalid argument for Relburnin\n", spacer);
					//free(tempStr);
					return (ERROR);
					}
				if (sumpParams.relativeBurnin == YES)
					MrBayesPrint ("%s   Using relative burnin (a fraction of samples discarded).\n", spacer);
				else
					MrBayesPrint ("%s   Using absolute burnin (a fixed number of samples discarded).\n", spacer);
				expecting = Expecting(PARAMETER) | Expecting(SEMICOLON);
				}
			else
				{
				//free (tempStr);
				return (ERROR);
				}
			}
		/* set Burnin (sumpParams.sumpBurnIn) ***********************************************************/
		else if (!strcmp(parmName, "Burnin"))
			{
			if (expecting == Expecting(EQUALSIGN))
				expecting = Expecting(NUMBER);
			else if (expecting == Expecting(NUMBER))
				{
				sscanf (tkn, "%d", &tempI);
                sumpParams.sumpBurnIn = tempI;
				MrBayesPrint ("%s   Setting sump burn-in to %d\n", spacer, sumpParams.sumpBurnIn);
				expecting = Expecting(PARAMETER) | Expecting(SEMICOLON);
				}
			else
				{
				//free(tempStr);
				return (ERROR);
				}
			}
		/* set Burninfrac (sumpParams.sumpBurnInFraction) ************************************************************/
		else if (!strcmp(parmName, "Burninfrac"))
			{
			if (expecting == Expecting(EQUALSIGN))
				expecting = Expecting(NUMBER);
			else if (expecting == Expecting(NUMBER))
				{
				sscanf (tkn, "%lf", &tempD);
				if (tempD < 0.01)
					{
					MrBayesPrint ("%s   Burnin fraction too low (< 0.01)\n", spacer);
					//free(tempStr);
					return (ERROR);
					}
				if (tempD > 0.50)
					{
					MrBayesPrint ("%s   Burnin fraction too high (> 0.50)\n", spacer);
					//free(tempStr);
					return (ERROR);
					}
                sumpParams.sumpBurnInFraction = tempD;
				MrBayesPrint ("%s   Setting burnin fraction to %.2f\n", spacer, sumpParams.sumpBurnInFraction);
				expecting = Expecting(PARAMETER) | Expecting(SEMICOLON);
				}
			else 
				{
				//free(tempStr);
				return (ERROR);
				}
			}
		/* set Nruns (sumpParams.numRuns) *******************************************************/
		else if (!strcmp(parmName, "Nruns"))
			{
			if (expecting == Expecting(EQUALSIGN))
				expecting = Expecting(NUMBER);
			else if (expecting == Expecting(NUMBER))
				{
				sscanf (tkn, "%d", &tempI);
				if (tempI < 1)
					{
					MrBayesPrint ("%s   Nruns must be at least 1\n", spacer);
					return (ERROR);
					}
				else
					{
					sumpParams.numRuns = tempI;
					MrBayesPrint ("%s   Setting sump nruns to %d\n", spacer, sumpParams.numRuns);
					expecting = Expecting(PARAMETER) | Expecting(SEMICOLON);
					}
				}
			else
				return (ERROR);
			}
		/* set Allruns (sumpParams.allRuns) ********************************************************/
		else if (!strcmp(parmName, "Allruns"))
			{
			if (expecting == Expecting(EQUALSIGN))
				expecting = Expecting(ALPHA);
			else if (expecting == Expecting(ALPHA))
				{
				if (IsArgValid(tkn, tempStr) == NO_ERROR)
					{
					if (!strcmp(tempStr, "Yes"))
						sumpParams.allRuns = YES;
					else
						sumpParams.allRuns = NO;
					}
				else
					{
					MrBayesPrint ("%s   Invalid argument for allruns (valid arguments are 'yes' and 'no')\n", spacer);
					return (ERROR);
					}
				if (sumpParams.allRuns == YES)
					MrBayesPrint ("%s   Setting sump to print information for each run\n", spacer);
				else
					MrBayesPrint ("%s   Setting sump to print only summary information for all runs\n", spacer);
				expecting = Expecting(PARAMETER) | Expecting(SEMICOLON);
				}
			else
				return (ERROR);
			}
		else
			return (ERROR);
		}

	return (NO_ERROR);

}





/* ExamineSumpFile: Collect info on the parameter samples in the file */
int ExamineSumpFile (char *fileName, SumpFileInfo *fileInfo, char ***headerNames, int *nHeaders)
{
    char    *sumpTokenP, sumpToken[CMD_STRING_LENGTH], *s=NULL, *headerLine, *t;
    int     i, lineTerm, inSumpComment, lineNum, lastNonDigitLine, numParamLines, allDigitLine,
            lastTokenWasDash, nNumbersOnThisLine, tokenType, burnin, nLines, firstNumCols,
            numRows, numColumns;
    MrBFlt  tempD;
    FILE    *fp;


	/* open binary file */
	if ((fp = OpenBinaryFileR(fileName)) == NULL)
        {
        /* test for some simple errors */
        if (strlen(sumpParams.sumpFileName) > 2)
            {
            s = sumpParams.sumpFileName + (int) strlen(sumpParams.sumpFileName) - 2;
		    if (strcmp(s, ".p") == 0)
		        {
		        MrBayesPrint ("%s   It appears that you need to remove '.p' from the 'Filename' parameter\n", spacer);
		        MrBayesPrint ("%s   Also make sure that 'Nruns' is set correctly\n", spacer);
		        return ERROR;
                }
            }
        MrBayesPrint ("%s   Make sure that 'Nruns' is set correctly\n", spacer);
		return ERROR;
        }
    
	/* find out what type of line termination is used */
	lineTerm = LineTermType (fp);
	if (lineTerm != LINETERM_MAC && lineTerm != LINETERM_DOS && lineTerm != LINETERM_UNIX)
		{
		MrBayesPrint ("%s   Unknown line termination\n", spacer);
		goto errorExit;
		}
		
	/* find length of longest line */
	fileInfo->longestLineLength = LongestLine (fp);

	/* allocate string long enough to hold a line */
	s = (char *)SafeMalloc((size_t) (2*(fileInfo->longestLineLength + 10) * sizeof(char)));
	if (!s)
		{
		MrBayesPrint ("%s   Problem allocating string for reading sump file\n", spacer);
		goto errorExit;
		}
    headerLine = s + fileInfo->longestLineLength + 10;

	/* close binary file */
	SafeFclose (&fp);
	
	/* open text file */
	if ((fp = OpenTextFileR(fileName)) == NULL)
		goto errorExit;
	
	/* Check file for appropriate blocks. We want to find the last block
	   in the file and start from there. */
	inSumpComment = NO;
	lineNum = lastNonDigitLine = numParamLines = 0;
	while (fgets (s, fileInfo->longestLineLength + 1, fp) != NULL)
        {
		sumpTokenP = &s[0];
		allDigitLine = YES;
		lastTokenWasDash = NO;
		nNumbersOnThisLine = 0;
		do {
			GetToken (sumpToken, &tokenType, &sumpTokenP);

            /*printf ("%s (%d)\n", sumpToken, tokenType);*/
			if (IsSame("[", sumpToken) == SAME)
				inSumpComment = YES;
			if (IsSame("]", sumpToken) == SAME)
				inSumpComment = NO;
					
			if (inSumpComment == NO)
				{
				if (tokenType == NUMBER)
					{
					sscanf (sumpToken, "%lf", &tempD);
					if (lastTokenWasDash == YES)
						tempD *= -1.0;
					nNumbersOnThisLine++;
					lastTokenWasDash = NO;
					}
				else if (tokenType == DASH)
					{
					lastTokenWasDash = YES;
					}
				else if (tokenType != UNKNOWN_TOKEN_TYPE)
					{
					allDigitLine = NO;
					lastTokenWasDash = NO;
					}
				}
			} while (*sumpToken);
		lineNum++;
		
		if (allDigitLine == NO)
			{
			lastNonDigitLine = lineNum;
			numParamLines = 0;
			}
		else
			{
			if (nNumbersOnThisLine > 0)
				numParamLines++;
			}
		}
		
	/* Now, check some aspects of the .p file. */
	if (inSumpComment == YES)
		{
		MrBayesPrint ("%s   Unterminated comment in file \"%s\"\n", spacer, fileName);
			goto errorExit;
		}
	if (numParamLines <= 0)
		{
		MrBayesPrint ("%s   No parameters were found in file \"%s\"\n", spacer, fileName);
			goto errorExit;
		}

    /* calculate burnin */
    if (sumpParams.relativeBurnin == YES)
        burnin = (int) (sumpParams.sumpBurnInFraction * numParamLines);
    else
        burnin = sumpParams.sumpBurnIn;
    
	/* check against burnin */
	if (burnin > numParamLines)
		{
		MrBayesPrint ("%s   No parameters can be sampled from file %s as the burnin (%d) exceeds the number of lines in last block (%d)\n",
            spacer, fileName, burnin, numParamLines);
		MrBayesPrint ("%s   Try setting burnin to a number less than %d\n", spacer, numParamLines);
		goto errorExit;
		}

	/* Set some info in fileInfo */
    fileInfo->firstParamLine = lastNonDigitLine + burnin;
    fileInfo->headerLine = lastNonDigitLine;

    /* Calculate and check the number of columns and rows for the file; get header line at the same time */
	(void)fseek(fp, 0L, 0);
	for (lineNum=0; lineNum<lastNonDigitLine; lineNum++)
	    if(fgets (s, fileInfo->longestLineLength + 1, fp)==NULL)
            goto errorExit;
    strcpy(headerLine, s);
    for (; lineNum < lastNonDigitLine+burnin; lineNum++)
	    if(fgets (s, fileInfo->longestLineLength + 1, fp)==NULL)
            goto errorExit;

	inSumpComment = NO;
	nLines = 0;
	numRows = numColumns = 0;
	while (fgets (s, fileInfo->longestLineLength + 1, fp) != NULL)
        {
		sumpTokenP = &s[0];
		allDigitLine = YES;
		lastTokenWasDash = NO;
		nNumbersOnThisLine = 0;
		do {
			GetToken (sumpToken, &tokenType, &sumpTokenP);
			if (IsSame("[", sumpToken) == SAME)
				inSumpComment = YES;
			if (IsSame("]", sumpToken) == SAME)
				inSumpComment = NO;
			if (inSumpComment == NO)
				{
				if (tokenType == NUMBER)
					{
					nNumbersOnThisLine++;
					lastTokenWasDash = NO;
					}
				else if (tokenType == DASH)
					{
					lastTokenWasDash = YES;
					}
				else if (tokenType != UNKNOWN_TOKEN_TYPE)
					{
					allDigitLine = NO;
					lastTokenWasDash = NO;
					}
				}
			} while (*sumpToken);
		lineNum++;
		if (allDigitLine == NO)
			{
			MrBayesPrint ("%s   Found a line with non-digit characters (line %d) in file %s\n", spacer, lineNum, fileName);
			goto errorExit;
			}
		else
			{
			if (nNumbersOnThisLine > 0)
				{
				nLines++;
				if (nLines == 1)
					firstNumCols = nNumbersOnThisLine;
				else
					{
					if (nNumbersOnThisLine != firstNumCols)
						{
						MrBayesPrint ("%s   Number of columns is not even (%d in first line and %d in %d line of file %s)\n", spacer, firstNumCols, nNumbersOnThisLine, lineNum, fileName);
						goto errorExit;
						}
					}
				}
            }
		}
	fileInfo->numRows = nLines;
	fileInfo->numColumns = firstNumCols;

    /* set or check headers */
    if ((*headerNames) == NULL)
        {
        GetHeaders (headerNames, headerLine, nHeaders);
        if (*nHeaders != fileInfo->numColumns)
            {
			MrBayesPrint ("%s   Expected %d headers but found %d headers\n", spacer, fileInfo->numColumns, *nHeaders);
            for (i=0; i<*nHeaders; i++)
                SafeFree ((void **) &((*headerNames)[i]));
            SafeFree ((void **) &(*headerNames));
            goto errorExit;
            }
        }
    else
        {
        if (*nHeaders != fileInfo->numColumns)
            {
			MrBayesPrint ("%s   Expected %d columns but found %d columns\n", spacer, *nHeaders, fileInfo->numColumns);
            goto errorExit;
            }
        for (i=0, t=strtok(headerLine,"\t\n\r"); t!=NULL; t=strtok(NULL,"\t\n\r"), i++)
            {
            if (i == *nHeaders)
                break;
            if (strcmp(t,(*headerNames)[i])!=0)
                break;
            }
        if (t != NULL)
            {
            MrBayesPrint ("%s   Expected %d headers but found more headers\n",
                spacer, fileInfo->numColumns);
            goto errorExit;
            }
        if (i < *nHeaders)
            {
            MrBayesPrint ("%s   Expected header '%s' for column %d but the header for this column was '%s' in file '%s'\n",
                spacer, headerNames[i], i+1, t, fileName);
            goto errorExit;
            }
        }

    free (s);
    return (NO_ERROR);

errorExit:

    free(s);
    return (ERROR);
}





/***************************************************
|
|   FindHeader: Find token in list
|
----------------------------------------------------*/
int FindHeader (char *token, char **headerNames, int nHeaders, int *index)
{
    int         i, match, nMatches;

    *index = -1;
    nMatches = 0;
    for (i=0; i<nHeaders; i++)
        {
        if (!strcmp(token,headerNames[i]))
            {
            nMatches++;
            match = i;
            }
        }

    if (nMatches != 1)
        return (ERROR);

    *index = match;
	return (NO_ERROR);	
}





/* FreeParameterSamples: Free parameter samples space */
void FreeParameterSamples (ParameterSample *parameterSamples)
{
    if (parameterSamples != NULL)
        {
        free (parameterSamples[0].values[0]);
        free (parameterSamples[0].values);
        free (parameterSamples);
        }
}





/***************************************************
|
|   GetHeaders: Get headers from headerLine and put
|      them in list while updating nHeaders to reflect
|      the number of headers
|
----------------------------------------------------*/
int GetHeaders (char ***headerNames, char *headerLine, int *nHeaders)
{
	char		*s;

    (*nHeaders) = 0;
	for (s=strtok(headerLine," \t\n\r"); s!=NULL; s=strtok(NULL," \t\n\r"))
		{
		if (AddString (headerNames, *nHeaders, s) == ERROR)
			{
			MrBayesPrint ("%s   Error adding header to list of headers \n", spacer, s);
			return ERROR;
			}
		(*nHeaders)++;
        }
		
#	if 0
	for (i=0; i<(*nHeaders); i++)
		printf ("%4d -> '%s'\n", i, headerNames[i]);
#	endif
		
	return (NO_ERROR);	
}





/* PrintModelStats: Print model stats to screen and to .mstat file */
int PrintModelStats (char *fileName, char **headerNames, int nHeaders, ParameterSample *parameterSamples, int nRuns, int nSamples)
{
	int		i, j, j1, j2, k, longestName, nElements, *modelCounts=NULL;
	MrBFlt	f, *prob=NULL, *sum=NULL, *ssq=NULL, *min=NULL, *max=NULL, *stddev=NULL;
	char	temp[100];
    FILE    *fp;

    /* nHeaders - is a convenient synonym for number of column headers */


    /* check if we have any model indicator variables and also check for longest header */
    k = 0;
    longestName = 0;
    for (i=0; i<nHeaders; i++)
        {
        for (j=0; strcmp(modelIndicatorParams[j],"")!=0; j++)
            {
            if (IsSame (headerNames[i], modelIndicatorParams[j]) != DIFFERENT)
                {
                k++;
                for (j1=0; strcmp(modelElementNames[j][j1],"")!=0; j1++)
                    {
                    j2 = (int)(strlen(headerNames[i]) + 2 + strlen(modelElementNames[j][j1]));
                    if (j2 > longestName)
                        longestName = j2;
                    }
                break;
                }
            }
        }

    /* return if nothing to do */
    if (k==0)
        return NO_ERROR;

    /* open output file */
    strncpy (temp,fileName,90);
    strcat (temp, ".mstat");
    fp = OpenNewMBPrintFile(temp);
    if (!fp)
        return ERROR;

    /* print unique identifier to the output file */
	if (strlen(stamp) > 1)
		fprintf (fp, "[ID: %s]\n", stamp);

    /* print header */
	MrBayesPrintf (fp, "\n\n");
    if (nRuns == 1)
        {
        MrBayesPrint ("%s        %*c        Posterior\n", spacer, longestName-5, ' ');
        MrBayesPrint ("%s   Model%*c       Probability\n", spacer, longestName-5, ' ');
        MrBayesPrint ("%s   -----%*c------------------\n", spacer, longestName-5, '-');
        MrBayesPrintf (fp, "Model\tProbability\n");
        }
    else
        {
		MrBayesPrint ("%s        %*c        Posterior      Standard         Min.           Max.   \n", spacer, longestName-5, ' ');
		MrBayesPrint ("%s   Model%*c       Probability     Deviation     Probability    Probability\n", spacer, longestName-5, ' ');
        MrBayesPrint ("%s   -----%*c---------------------------------------------------------------\n", spacer, longestName-5, '-');
        MrBayesPrintf (fp, "Model\tProbability\tStd_dev\tMin_prob\tMax_prob\n");
        }

    /* calculate and print values */
    for (i=0; i<nHeaders; i++)
		{
        for (j=0; modelIndicatorParams[j][0]!='\0'; j++)
            if (IsSame (headerNames[i], modelIndicatorParams[j]) != DIFFERENT)
                break;
        if (modelIndicatorParams[j][0] == '\0')
            continue;

        for (nElements=0; modelElementNames[j][nElements][0]!='\0'; nElements++)
            ;
        
        modelCounts = (int *) SafeCalloc (nElements, sizeof(int));
        if (!modelCounts)
            {
            fclose(fp);
            return ERROR;
            }
        prob = (MrBFlt *) SafeCalloc (6*nElements, sizeof(MrBFlt));
        if (!prob)
            {
            free (modelCounts);
            fclose (fp);
            return ERROR;
            }
        sum    = prob + nElements;
        ssq    = prob + 2*nElements;
        stddev = prob + 3*nElements;
        min    = prob + 4*nElements;
        max    = prob + 5*nElements;

        for (j1=0; j1<nElements; j1++)
            min[j1] = 1.0;
        for (j1=0; j1<nRuns; j1++)
            {
            for (j2=0; j2<nElements; j2++)
                modelCounts[j2] = 0;
            for (j2=0; j2<nSamples; j2++)
                modelCounts[(int)(parameterSamples[i].values[j1][j2] + 0.1)]++;
            for (j2=0; j2<nElements; j2++)
                {
                f = (MrBFlt) modelCounts[j2] / (MrBFlt) nRuns;
                sum[j2] += f;
                ssq[j2] += f*f;
                if (f<min[j2])
                    min[j2] = f;
                if (f > max[j2])
                    max[j2] = f;
                }
            }
		for (j1=0; j1<nElements; j1++)
			{
            prob[j1] = sum[j1] / (MrBFlt) nRuns;
			f = ssq[j] - (sum[j] * sum[j] / (MrBFlt) nRuns);
			f /= (nRuns - 1);
			if (f <= 0.0)
				stddev[j] = 0.0;
			else
				stddev[j] = sqrt (f);
			}

        if (nRuns == 1)
            {
            sprintf (temp, "%s[%s]", headerNames[i], modelElementNames[i][j]);
			MrBayesPrint ("%s   %.*s          %1.3lf\n", spacer, longestName, temp, prob[i]);
            MrBayesPrintf (fp, "%s\t%s\n", temp, MbPrintNum(prob[i])); 
            }
        else /* if (nRuns > 1) */
            {
            sprintf (temp, "%s[%s]", headerNames[i], modelElementNames[i][j]);
			MrBayesPrint ("%s   %.*s          %1.3lf          %1.3lf          %1.3lf          %1.3lf\n", 
                spacer, longestName, temp, prob[i], stddev[i], min[i], max[i]);
            MrBayesPrintf (fp, "%s", temp);
            MrBayesPrintf (fp, "\t%s", MbPrintNum(prob[i]));
            MrBayesPrintf (fp, "\t%s", MbPrintNum(stddev[i]));
            MrBayesPrintf (fp, "\t%s", MbPrintNum(min[i]));
            MrBayesPrintf (fp, "\t%s", MbPrintNum(max[i]));
            MrBayesPrintf (fp, "\n");
            }

        SafeFree ((void **) &modelCounts);
        SafeFree ((void **) &prob);
		}

	/* print footer */
    if (nRuns == 1)
        {
        MrBayesPrint ("%s   -----%*c------------------\n\n", spacer, longestName-5, '-');
        }
    else
        {
        MrBayesPrint ("%s   -----%*c---------------------------------------------------------------\n\n", spacer, longestName-5, '-');
        }

    /* close output file */
    fclose (fp);

	return (NO_ERROR);
}





/* PrintOverlayPlot: Print overlay x-y plot of log likelihood vs. generation for several runs */
int PrintOverlayPlot (MrBFlt **xVals, MrBFlt **yVals, int nRuns, int nSamples)
{
	int		i, j, k, k2, n, screenHeight, screenWidth, numY[60];
	char	plotSymbol[15][60];
	MrBFlt	x, y, minX, maxX, minY, maxY, meanY[60];
	
	if (nRuns == 2)
		MrBayesPrint ("\n%s   Overlay plot for both runs:\n", spacer);
	else
		MrBayesPrint ("\n%s   Overlay plot for all %d runs:\n", spacer, sumpParams.numRuns);
	if (nRuns > 9)
		MrBayesPrint ("%s   (1 = Run number 1; 2 = Run number 2 etc.; x = Run number 10 or above; * = Several runs)\n", spacer);
	else if (nRuns > 2)
		MrBayesPrint ("%s   (1 = Run number 1; 2 = Run number 2 etc.; * = Several runs)\n", spacer);
	else
		MrBayesPrint ("%s   (1 = Run number 1; 2 = Run number 2; * = Both runs)\n", spacer);

	/* print overlay x-y plot of log likelihood vs. generation for all runs */
	screenWidth = 60; /* don't change this without changing numY, meanY, and plotSymbol declared above */
	screenHeight = 15;

	/* find minX, minY, maxX, and maxY over all runs */
	minX = minY = 1000000000.0;
	maxX = maxY = -1000000000.0;
	for (n=0; n<nRuns; n++)
		{
		for (i=0; i<nSamples; i++)
			{
			x = xVals[n][i];
			if (x < minX)
				minX = x;
			if (x > maxX)
				maxX = x;
			}
		}
	for (n=0; n<nRuns; n++)
		{
		y = 0.0;
		j = 0;
		k2 = 0;
		for (i=0; i<nSamples; i++)
			{
			x = xVals[n][i];
			k = (int) (((x - minX) / (maxX - minX)) * screenWidth);
			if (k <= 0)
				k = 0;
			if (k >= screenWidth)
				k = screenWidth - 1;
			if (k == j)
				{
				y += yVals[n][i];
				k2 ++;
				}
			else
				{
				y /= k2;
				if (y < minY)
					minY = y;
				if (y > maxY)
					maxY = y;
				k2 = 1;
				y = yVals[n][i];
				j++;
				}
			}
		if (k2 > 0)
			{
			y /= k2;
			if (y < minY)
				minY = y;
			if (y > maxY)
				maxY = y;
			}
		}
	
	/* initialize the plot symbols */
	for (i=0; i<screenHeight; i++)
		for (j=0; j<screenWidth; j++)
			plotSymbol[i][j] = ' ';

	/* assemble the plot symbols */
	for (n=0; n<nRuns; n++)
		{
		/* find the plot points for this run */
		for (i=0; i<screenWidth; i++)
			{
			numY[i] = 0;
			meanY[i] = 0.0;
			}
		for (i=0; i<nSamples; i++)
			{
			x = xVals[n][i];
			y = yVals[n][i];
			k = (int)(((x - minX) / (maxX - minX)) * screenWidth);
			if (k >= screenWidth)
				k = screenWidth - 1;
			if (k <= 0)
				k = 0;
			meanY[k] += y;
			numY[k]++;
			}

		/* transfer these points to the overlay */
		for (i=0; i<screenWidth; i++)
			{
			if (numY[i] > 0)
				{
				k = (int) ((((meanY[i] / numY[i]) - minY)/ (maxY - minY)) * screenHeight);
				if (k < 0)
					k = 0;
				else if (k >= screenHeight)
					k = screenHeight - 1;
				if (plotSymbol[k][i] == ' ')
					{
					if (n <= 8)
						plotSymbol[k][i] = '1' + n;
					else
						plotSymbol[k][i] = 'x';
					}
				else
					plotSymbol[k][i] = '*';
				}
			}
		} /* next run */

	/* now print the overlay plot */
	MrBayesPrint ("\n   +");
	for (i=0; i<screenWidth; i++)
		MrBayesPrint ("-");
	MrBayesPrint ("+ %1.2lf\n", maxY);
	for (i=screenHeight-1; i>=0; i--)
		{
		MrBayesPrint ("   |");
		for (j=0; j<screenWidth; j++)
			{
			MrBayesPrint ("%c", plotSymbol[i][j]);
			}
		MrBayesPrint ("|\n");
		}
	MrBayesPrint ("   +");
	for (i=0; i<screenWidth; i++)
		{
		if (i % (screenWidth/10) == 0 && i != 0)
			MrBayesPrint ("+");
		else
			MrBayesPrint ("-");
		}
	MrBayesPrint ("+ %1.2lf\n", minY);
	MrBayesPrint ("   ^");
	for (i=0; i<screenWidth; i++)
		MrBayesPrint (" ");
	MrBayesPrint ("^\n");
	MrBayesPrint ("   %1.0lf", minX);
	for (i=0; i<screenWidth-(int)(log10(minX)); i++)
		MrBayesPrint (" ");
	MrBayesPrint ("%1.0lf\n\n", maxX);

	return (NO_ERROR);
}





/* PrintParamStats: Print parameter table (not model indicator params) to screen and .pstat file */
int PrintParamStats (char *fileName, char **headerNames, int nHeaders, ParameterSample *parameterSamples, int nRuns, int nSamples)
{
	int		i, j, len, longestHeader, *sampleCounts=NULL;
	char	temp[100];
    Stat    theStats;
    FILE    *fp;
	
	/* calculate longest header */
	longestHeader = 9;	/* length of 'parameter' */
	for (i=0; i<nHeaders; i++)
		{
        strcpy (temp, headerNames[i]);
		len = (int) strlen(temp);
        for (j=0; modelIndicatorParams[j][0]!='\0'; j++)
            if (IsSame (temp,modelIndicatorParams[j]) != DIFFERENT)
                break;
        if (modelIndicatorParams[j][0]!='\0')
            continue;
		if (!strcmp (temp, "Gen"))
			continue;
		if (!strcmp (temp, "lnL") == SAME)
			continue;
		if (len > longestHeader)
			longestHeader = len;
		}
	
    /* open output file */
    strncpy (temp, fileName, 90);
    strcat (temp, ".pstat");
    fp = OpenNewMBPrintFile (temp);
    if (!fp)
        return ERROR;

    /* print unique identifier to the output file */
	if (strlen(stamp) > 1)
		fprintf (fp, "[ID: %s]\n", stamp);

    /* allocate and set nSamples */
    sampleCounts = (int *) SafeCalloc (nRuns, sizeof(int));
    if (!sampleCounts)
        {
        fclose(fp);
        return ERROR;
        }
    for (i=0; i<nRuns; i++)
        sampleCounts[i] = nSamples;

    /* print the header rows */
    MrBayesPrint("\n");
	if (sumpParams.HPD == YES)
        MrBayesPrint ("%s   %*c                             95%% HPD Interval\n", spacer, longestHeader, ' ');
    else
        MrBayesPrint ("%s   %*c                             95%% Cred. Interval\n", spacer, longestHeader, ' ');
	MrBayesPrint ("%s   %*c                           --------------------\n", spacer, longestHeader, ' ');

	MrBayesPrint ("%s   Parameter%*c    Mean      Variance     Lower       Upper       Median", spacer, longestHeader-9, ' ');
	if (nRuns > 1)
		MrBayesPrint ("     PSRF+ ");
	MrBayesPrint ("\n");

	MrBayesPrint ("%s   ", spacer);
	for (j=0; j<longestHeader+1; j++)
		MrBayesPrint ("-");
	MrBayesPrint ("----------------------------------------------------------");
	if (nRuns > 1)
		MrBayesPrint ("----------");
	MrBayesPrint ("\n");
    if (nRuns > 1)
        MrBayesPrintf (fp, "Parameter\tMean\tVariance\tLower\tUpper\tMedian\tPSRF\n");
    else
        MrBayesPrintf (fp, "Parameter\tMean\tVariance\tLower\tUpper\tMedian\n");

	/* print table values */
	for (i=0; i<nHeaders; i++)
		{
        strcpy (temp, headerNames[i]);
		len = (int) strlen(temp);
        for (j=0; modelIndicatorParams[j][0]!='\0'; j++)
            if (IsSame (temp,modelIndicatorParams[j]) != DIFFERENT)
                break;
		if (IsSame (temp, "Gen") == SAME)
			continue;
		if (IsSame (temp, "lnL") == SAME)
			continue;

		GetSummary (parameterSamples[i].values, nRuns, sampleCounts, &theStats, sumpParams.HPD);
		
		MrBayesPrint ("%s   %-*s ", spacer, longestHeader, temp);
		MrBayesPrint ("%10.6lf  %10.6lf  %10.6lf  %10.6lf  %10.6lf", theStats.mean, theStats.var, theStats.lower, theStats.upper, theStats.median);
		MrBayesPrintf (fp, "%s", temp);
		MrBayesPrintf (fp, "\t%s", MbPrintNum(theStats.mean));
		MrBayesPrintf (fp, "\t%s", MbPrintNum(theStats.var));
		MrBayesPrintf (fp, "\t%s", MbPrintNum(theStats.lower));
		MrBayesPrintf (fp, "\t%s", MbPrintNum(theStats.upper));
		MrBayesPrintf (fp, "\t%s", MbPrintNum(theStats.median));
		if (nRuns > 1)
			{
			if (theStats.PSRF < 0.0)
                {
				MrBayesPrint ("       N/A  ");
                MrBayesPrintf (fp, "NA");
                }
			else
                {
				MrBayesPrint ("  %7.3lf", theStats.PSRF);
                MrBayesPrintf (fp, "\t%s", MbPrintNum(theStats.PSRF));
                }
			}
		MrBayesPrint ("\n");
		MrBayesPrintf (fp, "\n");
		}
	MrBayesPrint ("%s   ", spacer);
	for (j=0; j<longestHeader+1; j++)
		MrBayesPrint ("-");
	MrBayesPrint ("----------------------------------------------------------");
	if (nRuns > 1)
		MrBayesPrint ("----------");
	MrBayesPrint ("\n");
	if (nRuns > 1)
		{
		MrBayesPrint ("%s   + Convergence diagnostic (PSRF = Potential scale reduction factor; Gelman\n", spacer);
		MrBayesPrint ("%s     and Rubin, 1992) should approach 1 as runs converge.\n", spacer);
		}

    fclose (fp);
    free (sampleCounts);

    return (NO_ERROR);
}





/* PrintPlot: Print x-y plot of log likelihood vs. generation */
int PrintPlot (MrBFlt *xVals, MrBFlt *yVals, int numVals)
{
	int		i, j, k, numY[60], screenWidth, screenHeight;
	MrBFlt	x, y, minX, maxX, minY, maxY, meanY[60], diff;
					
	/* print x-y plot of log likelihood vs. generation */
	screenWidth = 60; /* don't change this without changing numY and meanY, declared above */
	screenHeight = 15;

	/* find minX and maxX */
	minX = 1E10;
	maxX = -1E10;
	for (i=0; i<numVals; i++)
		{
		x = xVals[i];
		if (x < minX)
			minX = x;
		if (x > maxX)
			maxX = x;
		}

	/* collect Y data */
	for (i=0; i<screenWidth; i++)
		{
		numY[i] = 0;
		meanY[i] = 0.0;
		}
	for (i=0; i<numVals; i++)
		{
		x = xVals[i];
		y = yVals[i];
		k = (int)(((x - minX) / (maxX - minX)) * screenWidth);
		if (k >= screenWidth)
			k = screenWidth - 1;
		if (k < 0)
			k = 0;
		meanY[k] += y;
		numY[k]++;
		}

	/* find minY and maxY */
	minY = 1E10;
	maxY = -1E10;
	for (i=0; i<screenWidth; i++)
		{
		meanY[i] /= numY[i];
		if (meanY[i] < minY)
			minY = meanY[i];
		if (meanY[i] > maxY)
			maxY = meanY[i];
		}

    /* make some adjustments for graph to look good */
    diff = maxY - minY;
	if (diff < 1E-6)
		{
		maxY = meanY[0]/numY[0] + (MrBFlt) 0.1;
		minY = meanY[0]/numY[0] - (MrBFlt) 0.1;
		} 
	else
		{
		diff = maxY - minY;
		maxY += diff * (MrBFlt) 0.025;
		minY -= diff * (MrBFlt) 0.025;
		}

    /* print plot */
	MrBayesPrint ("\n   +");
	for (i=0; i<screenWidth; i++)
		MrBayesPrint ("-");
	MrBayesPrint ("+ %1.2lf\n", maxY);
	for (j=screenHeight-1; j>=0; j--)
		{
		MrBayesPrint ("   |");
		for (i=0; i<screenWidth; i++)
			{
			if (numY[i] > 0)
				{
				if (meanY[i] / numY[i] > (((maxY - minY)/screenHeight)*j)+minY && meanY[i] / numY[i] <= (((maxY - minY)/screenHeight)*(j+1))+minY)
					MrBayesPrint ("*");
				else
					MrBayesPrint (" ");
				}
			else
				{
				MrBayesPrint (" ");
				}
			}
		MrBayesPrint ("|\n");
		}
	MrBayesPrint ("   +");
	for (i=0; i<screenWidth; i++)
		{
		if (i % (screenWidth/10) == 0 && i != 0)
			MrBayesPrint ("+");
		else
			MrBayesPrint ("-");
		}
	MrBayesPrint ("+ %1.2lf\n", minY);
	MrBayesPrint ("   ^");
	for (i=0; i<screenWidth; i++)
		MrBayesPrint (" ");
	MrBayesPrint ("^\n");
	MrBayesPrint ("   %1.0lf", minX);
	for (i=0; i<screenWidth-(int)(log10(minX)); i++)
		MrBayesPrint (" ");
	MrBayesPrint ("%1.0lf\n\n", maxX);

	return (NO_ERROR);
}





void PrintPlotHeader (void)
{
	MrBayesPrint ("\n");
	if (sumpParams.numRuns > 1)
		{
		MrBayesPrint ("%s   Below are rough plots of the generation (x-axis) versus the log   \n", spacer);
		MrBayesPrint ("%s   probability of observing the data (y-axis). You can use these     \n", spacer);
		MrBayesPrint ("%s   graphs to determine what the burn in for your analysis should be. \n", spacer);
		MrBayesPrint ("%s   When the log probability starts to plateau you may be at station- \n", spacer);
		MrBayesPrint ("%s   arity. Sample trees and parameters after the log probability      \n", spacer);
		MrBayesPrint ("%s   plateaus. Of course, this is not a guarantee that you are at sta- \n", spacer);
		MrBayesPrint ("%s   tionarity. Also examine the convergence diagnostics provided by   \n", spacer);
		MrBayesPrint ("%s   the 'sump' and 'sumt' commands for all the parameters in your     \n", spacer);
		MrBayesPrint ("%s   model. Remember that the burn in is the number of samples to dis- \n", spacer);
		MrBayesPrint ("%s   card. There are a total of ngen / samplefreq samples taken during \n", spacer);
		MrBayesPrint ("%s   a MCMC analysis.                                                  \n", spacer);
		}
	else
		{
		MrBayesPrint ("%s   Below is a rough plot of the generation (x-axis) versus the log   \n", spacer);
		MrBayesPrint ("%s   probability of observing the data (y-axis). You can use this      \n", spacer);
		MrBayesPrint ("%s   graph to determine what the burn in for your analysis should be.  \n", spacer);
		MrBayesPrint ("%s   When the log probability starts to plateau you may be at station- \n", spacer);
		MrBayesPrint ("%s   arity. Sample trees and parameters after the log probability      \n", spacer);
		MrBayesPrint ("%s   plateaus. Of course, this is not a guarantee that you are at sta- \n", spacer);
		MrBayesPrint ("%s   analysis should be. When the log probability starts to plateau    \n", spacer);
		MrBayesPrint ("%s   tionarity. When possible, run multiple analyses starting from dif-\n", spacer);
		MrBayesPrint ("%s   ferent random trees; if the inferences you make for independent   \n", spacer);
		MrBayesPrint ("%s   analyses are the same, this is reasonable evidence that the chains\n", spacer);
		MrBayesPrint ("%s   have converged. You can use MrBayes to run several independent    \n", spacer);
		MrBayesPrint ("%s   analyses simultaneously. During such a run, MrBayes will monitor  \n", spacer);
		MrBayesPrint ("%s   the convergence of topologies. After the run has been completed,  \n", spacer);
		MrBayesPrint ("%s   the 'sumt' and 'sump' functions will provide additional conver-   \n", spacer);
		MrBayesPrint ("%s   gence diagnostics for all the parameters in your model. Remember  \n", spacer);
		MrBayesPrint ("%s   that the burn in is the number of samples to discard. There are   \n", spacer);
		MrBayesPrint ("%s   a total of ngen / samplefreq samples taken during a MCMC analysis.\n", spacer);
		}
}





/* ReadParamSamples: Read parameter samples from .p file */
int ReadParamSamples (char *fileName, SumpFileInfo *fileInfo, ParameterSample *parameterSamples, int runNo)
{
    char    sumpToken[CMD_STRING_LENGTH], *s=NULL, *p;
    int     inSumpComment, lineNum, numLinesRead, numLinesToRead, column, lastTokenWasDash,
            tokenType;
    MrBFlt  tempD;
    FILE    *fp;

    /* open file */
    if ((fp = OpenTextFileR (fileName)) == NULL)
        return ERROR;

    /* allocate space for reading lines */
    s = (char *) calloc (fileInfo->longestLineLength + 10, sizeof(char));

	/* fast forward to beginning of last unburned parameter line. */
	for (lineNum=0; lineNum<fileInfo->firstParamLine; lineNum++)
	  if (fgets (s, fileInfo->longestLineLength + 5, fp) == 0)
          goto errorExit;

    /* parse file, line-by-line. We are only parsing lines that have digits that should be read. */
	inSumpComment = NO;
	numLinesToRead = fileInfo->numRows;
	numLinesRead = 0;
	while (fgets (s, fileInfo->longestLineLength + 1, fp) != NULL)
		{
		lastTokenWasDash = NO;
        column = 0;
        p = s;
		do {
			GetToken (sumpToken, &tokenType, &p);
            if (IsSame("[", sumpToken) == SAME)
				inSumpComment = YES;
			if (IsSame("]", sumpToken) == SAME)
				inSumpComment = NO;
			if (inSumpComment == NO)
				{
				if (tokenType == NUMBER)
					{
					/* read the number */
					if (column >= fileInfo->numColumns)
						{
						MrBayesPrint ("%s   Too many values read on line %d of file %s\n", spacer, lineNum, fileName);
						goto errorExit;
						}
					sscanf (sumpToken, "%lf", &tempD);
					if (lastTokenWasDash == YES)
						tempD *= -1.0;
					parameterSamples[column].values[runNo][numLinesRead] = tempD;
					column++;
					lastTokenWasDash = NO;
					}
				else if (tokenType == DASH)
					{
					lastTokenWasDash = YES;
					}
				else if (tokenType != UNKNOWN_TOKEN_TYPE)
					{
					/* we have a problem */
					MrBayesPrint ("%s   Line %d of file %s has non-digit characters\n", spacer, lineNum, fileName);
					goto errorExit;
					}
				}
			} while (*sumpToken);

        lineNum++;
        if (column == fileInfo->numColumns)
			numLinesRead++;
        else if (column != 0)
            {
            MrBayesPrint ("%s   Too few values on line %d of file %s\n", spacer, lineNum, fileName);
			goto errorExit;
            }
        }

		
	/* Check how many parameter line was read in. */
	if (numLinesRead != numLinesToRead)
		{
		MrBayesPrint ("%s   Unable to read all lines that should contain parameter samples\n", spacer);
		goto errorExit;
		}

    fclose (fp);
    free (s);

    return (NO_ERROR);

errorExit:

    fclose (fp);
    free (s);

    return ERROR;
}




