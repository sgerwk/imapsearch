imapsearch
==========

Search an IMAP mailbox and possibly fetch and delete mails.

Examples:

* ``imapsearch -f Carl -s party -- gmail -20``;
	search for emails sent from Carl and mentioning "party" in the subject
	among the last 20 email

* ``imapsearch -a 12-Dec-2022 -v imapenvelope -- aol``;
	display all emails from Dec, 12 using the ``imapenvelope`` script to display them

* ``imapsearch -r 923:940 -p abcd -- protonmail``;
	save each mail from number 923 to 940 to files abcd.923, abcd.924, etc.

* ``imapsearch -w -d -f Julius -- yahoo``;
	delete all mails from Julius, asking confirmation for each
