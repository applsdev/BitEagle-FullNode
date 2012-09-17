BitEagle Full Node
==================

BitEagle Full Node is an implementation of a bitcoin fully validating node on-top of cbitcoin. It is currently in development at a pre-alpha stage. It is being designed with server usage in mind and is developed with POSIX libraries but the code may be adapted for various purposes. Please view http://cbitcoin.com to learn about the library BitEagle Full Node is dependent on.

BitEagle is the code-name for a new bitcoin client in development. The client will be lightweight and this fully validating code is for server-usage. The BitEagle clients will use a mixture of SPV and server validation for maximum security.

**To help support this development of BitEagle, including this code, please send donations to: 19LWPgTnUaGPav9Niuvb8FYh8PbCiMhccN Thank you**

Contributors
------------

The following list is for all people that have contributed work that has been accepted into BitEagle Full Node. Please consider making your own contribution to be added to the list.

Matthew Mitchell - 19LWPgTnUaGPav9Niuvb8FYh8PbCiMhccN

Making a Contribution
---------------------

1. Fork the project on github: https://github.com/MatthewLM/BitEagle-FullNode
2. Implement the changes.
3. Document the changes (See "Documenting" below)
4. Make a pull request.
5. Send an email to cbitcoin@thelibertyportal.com notifying that a request has been made.
6. The changes will be pulled once approved.

**Note that this code requires cbitcoin and should be compatibile with the latest code on the master branch. See https://github.com/MatthewLM/cbitcoin**

Documenting
-----------

BitEagle Full Node should contain the following header for each file:

	//
	//  BEFileName
	//  BitEagle-FullNode
	//
	//  Created by Full Name on DD/MM/YYYY.
	//  Copyright (c) 2012 Matthew Mitchell
	//  
	//  This file is part of BitEagle-FullNode.
	//
	//  BitEagle-FullNode is free software: you can redistribute it and/or modify
	//  it under the terms of the GNU General Public License as published by
	//  the Free Software Foundation, either version 3 of the License, or
	//  (at your option) any later version.
	//  
	//  BitEagle-FullNode is distributed in the hope that it will be useful,
	//  but WITHOUT ANY WARRANTY; without even the implied warranty of
	//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	//  GNU General Public License for more details.
	//  
	//  You should have received a copy of the GNU General Public License
	//  along with BitEagle-FullNode.  If not, see <http://www.gnu.org/licenses/>.

Header files should contain information for documentation. BitEagle Full Node uses a DoxyGen syntax (See http://www.stack.nl/~dimitri/doxygen/manual.html). Please document all files as well as structures and functions. Brief descriptions should be included. Details can be added at a later date, especailly once code has been properly implemented. Files should be documented like this:

	/**
	 @file
	 @brief Brief description of file.
	 @details More in depth description of file.
	 */

Structures should be documented like this:

	/**
	 @brief Brief decription of structure.
	 */
	typedef struct BESomeStruct{
		int someInt; /**< Description of data field. */
	} BESomeStruct;

Functions should be documented like this:

	/**
	 @brief Brief decription of function.
	 @param someParam Brief decription of the function parameter.
	 @returns What the function returns.
	 */
	int BESomeFunc(int someParam);
 
It may be that other elements are documented and should be documented in a similar fashion and please leave helpful comments in the source code.

Thank You!
==========