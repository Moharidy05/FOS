
#ifndef FOS_INC_QUEUE_H
#define FOS_INC_QUEUE_H


#if 0

struct Frob
{
	int frobozz;
	LIST_ENTRY(Frob) frob_link;	/* this contains the list element pointers */
};

LIST_HEAD(Frob_list, Frob)		/* defines struct Frob_list as a list of Frob */

struct Frob_list flist;			/* declare a Frob list */

LIST_INIT(&flist);			/* clear flist (globals are cleared anyway) */
flist = LIST_HEAD_INITIALIZER(&flist);	/* alternate way to clear flist */

if(LIST_EMPTY(&flist))			/* check whether list is empty */
	printf("list is empty\n");

struct Frob *f = LIST_FIRST(&flist);	/* f is first element in list */
f = LIST_NEXT(f, frob_link);		/* now f is next (second) element in list */
f = LIST_NEXT(f, frob_link);		/* now f is next (third) element in list */

for(f=LIST_FIRST(&flist); f != 0; 	/* iterate over elements in flist */
    f = LIST_NEXT(f, frob_link))
	printf("f %d\n", f->frobozz);

LIST_FOREACH(f, &flist, frob_link)	/* alternate way to say that */
	printf("f %d\n", f->frobozz);

f = LIST_NEXT(LIST_FIRST(&flist));	/* f is second element in list */
LIST_INSERT_AFTER(f, g, frob_link);	/* add g right after f in list */
LIST_REMOVE(g, frob_link);		/* remove g from list (can't insert twice!) */
LIST_INSERT_BEFORE(f, g, frob_link);	/* add g right before f */
LIST_REMOVE(g, frob_link);		/* remove g again */
LIST_INSERT_HEAD(&flist, g, frob_link);	/* add g as first element in list */

#endif


#define	LIST_HEAD(name, type)						\
struct name {								\
	struct type *lh_first;	/* first element */			\
	struct type *lh_last; \
	struct type *	___ptr_next; 	/* used as a temp saving place in LIST_FOREACH  */				\
	uint32 size;					/*maintained by list functions */ \
}

/*
 * Set a list head variable to LIST_HEAD_INITIALIZER(head)
 * to reset it to the empty list.
 */
#define	LIST_HEAD_INITIALIZER(head)					\
	{ NULL }

/*
 * Use this inside a structure "LIST_ENTRY(type) prev_next_info" to use
 * x as the list piece.
 *
 * The le_prev points at the pointer to the structure containing
 * this very LIST_ENTRY, so that if we want to remove this list entry,
 * we can do *le_prev = le_next to update the structure pointing at us.
 */
#define	LIST_ENTRY(type)						\
struct {								\
	struct type *le_next;	/* next element */			\
	struct type *le_prev;	/* ptr to prev element */	\
}

/*
 * List functions.
 */

/*
 * Is the list named "head" empty?
 */
#define	LIST_EMPTY(head)	((head)->lh_first == NULL)

/*
 * Return the first element in the list named "head".
 */
#define	LIST_FIRST(head)	((head)->lh_first)
#define	LIST_LAST(head)	((head)->lh_last)

#define	LIST_SIZE(head)	((head)->size)

/*
 * Return the element after "elm" in the list.
 * The "prev_next_info" name is the link element as above.
 */
//#define	LIST_NEXT(elm)	((elm)->prev_next_info.le_next)
#define	LIST_NEXT(elm)	((elm)->prev_next_info.le_next)

#define	LOOP_LIST_NEXT(elm)	( (elm == NULL ? NULL : elm->prev_next_info.le_next) )

/*
 * Return the element before "elm" in the list.
 * The "prev_next_info" name is the link element as above.
 */
#define	LIST_PREV(elm)	((elm)->prev_next_info.le_prev)

/*
 * Iterate over the elements in the list named "head".
 * During the loop, assign the list elements to the variable "var"
 * and use the LIST_ENTRY structure member "prev_next_info" as the link prev_next_info.
 */
/*
#define	LIST_FOREACH(var, head)					\
	for ((var) = LIST_FIRST((head));				\
	(var);							\
	(var) = LIST_NEXT((var)))
*/

//TODO: Warning CANNOT BE USED AS NESTED LOOP because of the temp ___ptr_next, NEEDS MODIFICATION
#define LIST_FOREACH(var, head)					\
	for ((var) = LIST_FIRST((head));				\
	( (head)->___ptr_next = LOOP_LIST_NEXT((var))) || (var);							\
	(var) = (head)->___ptr_next)

/*
 * Reset the list named "head" to the empty list.
 */
#define	LIST_INIT(head) do {						\
	LIST_FIRST((head)) = NULL;					\
	LIST_LAST((head)) = NULL;		\
	((head)->size) = 0;			\
} while (0)

/*
 * Insert the element "elm" *after* the element "listelm" which is
 * already in the list.  The "prev_next_info" name is the link element
 * as above.
 */
#define	LIST_INSERT_AFTER(list, listelm, elm) do {			\
	if ((listelm == NULL) || (elm == NULL)) panic("LIST_INSERT_AFTER: one of the two elements is NULL!");			\
	if ((LIST_NEXT((elm)) = LIST_NEXT((listelm))) != NULL)\
		LIST_PREV(LIST_NEXT((listelm))) =	((elm));	  \
	LIST_NEXT((listelm)) = ((elm));				\
	LIST_PREV((elm)) = (listelm);		\
	if( LIST_NEXT((elm)) == NULL)	\
	{	\
		LIST_LAST((list)) = ((elm));	\
	}	\
	((list)->size)++ ;			\
} while (0)

/*
 * Insert the element "elm" *before* the element "listelm" which is
 * already in the list.  The "prev_next_info" name is the link element
 * as above.
 */
#define	LIST_INSERT_BEFORE(list, listelm, elm) do {			\
	if ((listelm == NULL) || (elm == NULL)) panic("LIST_INSERT_BEFORE: one of the two elements is NULL!");			\
	LIST_PREV((elm)) = LIST_PREV((listelm));		\
	LIST_NEXT((elm)) = (listelm);				\
	if(LIST_PREV((listelm)) != NULL)	\
	{	\
		LIST_NEXT(LIST_PREV((listelm))) = (elm);				\
	}	\
	else	\
	{	\
		LIST_FIRST((list)) = (elm)	; 	\
	}	\
	LIST_PREV((listelm)) = ((elm));	\
	((list)->size)++ ;			\
} while (0)

/*
 * Insert the element "elm" at the head of the list named "head".
 * The "prev_next_info" name is the link element as above.
 */
#define	LIST_INSERT_HEAD(list, elm) do {				\
	if (elm == NULL) panic("LIST_INSERT_HEAD: element is NULL!");			\
	if ((LIST_NEXT((elm)) = LIST_FIRST((list))) != NULL)	\
		LIST_PREV(LIST_NEXT((elm))) = ((elm));\
	else	\
		LIST_LAST((list)) = (elm); \
	LIST_FIRST((list)) = (elm);					\
	LIST_PREV((elm)) = NULL;			\
	((list)->size)++ ;			\
} while (0)

/*
 * Insert the element "elm" at the tail of the list named "list".
 * The "prev_next_info" name is the link element as above.
 */
#define	LIST_INSERT_TAIL(list, elm) do {				\
	if (elm == NULL) panic("LIST_INSERT_TAIL: element is NULL!");			\
	if ((LIST_PREV((elm)) = LIST_LAST((list))) != NULL)	\
		LIST_NEXT(LIST_LAST((list)))= ((elm));\
	else	\
		LIST_FIRST((list)) = (elm); \
	LIST_LAST((list)) = (elm);					\
	LIST_NEXT((elm)) = NULL;			\
	((list)->size)++ ;			\
} while (0)

/*
 * Remove the element "elm" from the list.
 * The "prev_next_info" name is the link element as above.
 */
#define	LIST_REMOVE(list, elm) do {					\
	if (elm == NULL) panic("LIST_REMOVE: element is NULL!");			\
	if (LIST_NEXT((elm)) != NULL)				\
		LIST_PREV(LIST_NEXT((elm))) = LIST_PREV(elm);				\
	else	\
		LIST_LAST(list) = LIST_PREV(elm); \
	if(LIST_PREV(elm) != NULL)	\
		LIST_NEXT(LIST_PREV(elm)) = LIST_NEXT(elm);	\
	else	\
		LIST_FIRST((list)) = LIST_NEXT(elm); \
	LIST_NEXT((elm)) = NULL;			\
	LIST_PREV((elm)) = NULL;		\
	((list)->size)-- ;			\
} while (0)

#define	LIST_CONCAT(list1, list2) do {					\
	if(LIST_FIRST(list1) == NULL) {LIST_FIRST(list1) = LIST_FIRST(list2); LIST_LAST(list1) = LIST_LAST(list2); }\
    else if(LIST_FIRST(list2) != NULL)	\
    {	\
		LIST_NEXT(LIST_LAST(list1)) = LIST_FIRST(list2);	\
		LIST_PREV(LIST_FIRST(list2)) = LIST_LAST(list1);	\
		LIST_LAST(list1) = LIST_LAST(list2);	\
		LIST_INIT(list2); \
	}	\
	((list1)->size) += ((list2)->size);		\
} while (0)

//#define	LIST_CLEAR(list)	(LIST_INIT(list))

#define LIST_FOREACH_SAFE(var, head, field, tvar) \
    for ((var) = LIST_FIRST((head)); \
        (var) && ((tvar) = LIST_NEXT((var)), 1); \
        (var) = (tvar))
#endif	/* !_SYS_QUEUE_H_ */
