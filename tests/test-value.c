#include <stdlib.h>
#include <check.h>

#include "src/include/ngf/value.h"

START_TEST (test_value_create)
{
    NValue *value = NULL;
    int type = n_value_type (value);
    fail_unless (type == 0);
    value = n_value_new ();
    fail_unless (value != NULL);
	
    /* set type to integer */
    n_value_set_int (value, 100);
    type = n_value_type (value);
    fail_unless (type == N_VALUE_TYPE_INT);
	
    /* set memory to 0 */
    n_value_init (value);
    type = n_value_type (value);
    fail_unless (type == 0);

    n_value_free (value);
    value = NULL;
}
END_TEST

START_TEST (test_copy)
{
    NValue *val = NULL;
    NValue *copy = NULL;
    copy = n_value_copy (val);
    fail_unless (copy == NULL);
    val = n_value_new ();
    copy = n_value_copy (val);
    fail_unless (copy == NULL);
    gboolean result = FALSE;
    const char *str = "ngf";

    /* copy string value */
    n_value_set_string (val, str);
    copy = n_value_copy (val);
    result = n_value_equals (val, copy);
    fail_unless (result == TRUE);
    n_value_free (copy);
    copy = NULL;

    /* copy int value */
    n_value_set_int (val, -10);
    copy = n_value_copy (val);
    result = n_value_equals (val, copy);
    fail_unless (result == TRUE);
    n_value_free (copy);
    copy = NULL;

    /* copy uint value */
    n_value_set_uint (val, 10);
    copy = n_value_copy (val);
    result = n_value_equals (val, copy);
    fail_unless (result == TRUE);
    n_value_free (copy);
    copy = NULL;

    /* copy bool value */
    n_value_set_bool (val, TRUE);
    copy = n_value_copy (val);
    result = n_value_equals (val, copy);
    fail_unless (result == TRUE);
    n_value_free (copy);
    copy = NULL;

/* copy pointer value */
    n_value_set_pointer(val, val);
    copy = n_value_copy (val);
    result = n_value_equals (val, copy);
    fail_unless (result == TRUE);	
	
    n_value_free (val);
    val = NULL;
    n_value_free (copy);
    copy = NULL;
}
END_TEST

START_TEST (test_equals)
{
    NValue *valA = NULL;
    NValue *valB = NULL;
    gboolean result = TRUE;

    /* both are NULL */
    result = n_value_equals (valA, valB);
    fail_unless (result == FALSE);

    valA = n_value_new ();
    valB = n_value_new ();

    const char *str = "ngf";

    n_value_set_string (valA, str);
    n_value_set_uint (valB, 10);
    /* types differ */
    result = n_value_equals (valA, valB);
    fail_unless (result == FALSE);
	
    /* string types */
    n_value_set_string (valB, str);
    result = n_value_equals (valA, valB);
    fail_unless (result == TRUE);

    /* int types */
    n_value_set_int (valA, -10);
    n_value_set_int (valB, -10);
    result = n_value_equals (valA, valB);
    fail_unless (result == TRUE);

    /* uint types */
    n_value_set_uint (valA, 10);
    n_value_set_uint (valB, 10);
    result = n_value_equals (valA, valB);
    fail_unless (result == TRUE);

    /* boolean types */
    n_value_set_bool (valA, TRUE);
    n_value_set_bool (valB, TRUE);
    result = n_value_equals (valA, valB);
    fail_unless (result == TRUE);

    /* pointer types */
    n_value_set_pointer (valA, valA);
    n_value_set_pointer (valB, valA);
    result = n_value_equals (valA, valB);
    fail_unless (result == TRUE);

    /* different poiter */
    n_value_set_pointer (valB, valB);
    result = n_value_equals (valA, valB);
    fail_unless (result == FALSE);

    n_value_free (valA);
    valA = NULL;
    n_value_free (valB);
    valB = NULL;
}
END_TEST

START_TEST (test_string)
{
    NValue *value = NULL;
    value = n_value_new ();
    fail_unless (value != NULL);
    fail_unless (n_value_get_string (value) == NULL);
    const char *str_val = "NGF\0";
    n_value_set_string (value, str_val);
    const char *reply = n_value_get_string (value);
    fail_unless (reply != str_val);
    char *duplicate = n_value_dup_string (value);
    fail_unless (strcmp (duplicate, str_val) == 0);
	
    g_free (duplicate);
    duplicate = NULL;
    n_value_free (value);
    value = NULL;
}
END_TEST

START_TEST (test_int)
{
    NValue *value = NULL;
    value = n_value_new ();
    fail_unless (value != NULL);
    fail_unless (n_value_get_string (value) == 0);
    int val = -100;
    n_value_set_int (value, val);
    int reply = n_value_get_int (value);
    fail_unless (reply == val);

    n_value_free (value);
    value = NULL;
}
END_TEST

START_TEST (test_uint)
{
    NValue *value = NULL;
    value = n_value_new ();
    fail_unless (value != NULL);
    fail_unless (n_value_get_uint (value) == 0);
    uint val = 100;
    n_value_set_uint (value, val);
    uint reply = n_value_get_uint (value);
    fail_unless (reply == val);

    n_value_free (value);
    value = NULL;
}
END_TEST

START_TEST (test_bool)
{
    NValue *value = NULL;
    value = n_value_new ();
    fail_unless (value != NULL);
    fail_unless (n_value_get_bool (value) == FALSE);
    gboolean val = TRUE;
    n_value_set_bool (value, val);
    gboolean reply = n_value_get_bool (value);
    fail_unless (reply == val);

    n_value_free (value);
    value = NULL;
}
END_TEST

START_TEST (test_pointer)
{
    NValue *value = NULL;
    value = n_value_new ();
    fail_unless (value != NULL);
    fail_unless (n_value_get_pointer (value) == NULL);
    gpointer val = (gpointer)value;
    n_value_set_pointer (value, val);
    gpointer reply = n_value_get_pointer (value);
    fail_unless (reply == val);

    n_value_free (value);
    value = NULL;
}
END_TEST

START_TEST (test_to_string)
{
    NValue *value = NULL;
    value = n_value_new ();
    fail_unless (value != NULL);
    const char *str = "NGF";
    const int i = -100;
    const uint ui = 10;
    const gboolean logic = TRUE;
    const gpointer point = value;
    char *expected = "<unknown value>";

    char *string = n_value_to_string (value);
    fail_unless (strcmp (string, expected) == 0);
    g_free (string);
    string = NULL;
	
    n_value_set_string (value, str);
    string = n_value_to_string (value);
    expected = "NGF (string)";
    fail_unless (strcmp (string, expected) == 0);
    g_free (string);
    string = NULL;

    n_value_set_int (value, i);
    string = n_value_to_string (value);
    expected = "-100 (int)";
    fail_unless (strcmp (string, expected) == 0);
    g_free (string);
    string = NULL;

    n_value_set_uint (value, ui);
    string = n_value_to_string (value);
    expected = "10 (uint)";
    fail_unless (strcmp (string, expected) == 0);
    g_free (string);
    string = NULL;

    n_value_set_bool (value, logic);
    string = n_value_to_string (value);
    expected = "TRUE (bool)";
    fail_unless (strcmp (string, expected) == 0);
    g_free (string);
    string = NULL;

    n_value_set_pointer (value, point);
    string = n_value_to_string (value);
    expected = g_strdup_printf ("0x%p (pointer)", point);
    fail_unless (g_strcmp0 (string, expected) == 0);
    g_free (string);
    g_free (expected);
    string = NULL;

    n_value_free (value);
    value = NULL;
}
END_TEST

int
main (int agrc, char* argv[])
{
    (void) agrc;
    (void) argv;

    int num_failed = 0;
    Suite *s = NULL;
    TCase *tc = NULL;
    SRunner *sr = NULL;

    s = suite_create ("\tValue tests");

    tc = tcase_create ("Create");
    tcase_add_test (tc, test_value_create);
    suite_add_tcase (s, tc);

    tc = tcase_create ("NValues copy");
    tcase_add_test (tc, test_copy);
    suite_add_tcase (s, tc);
	
    tc = tcase_create ("NValues equals");
    tcase_add_test (tc, test_equals);
    suite_add_tcase (s, tc);

    tc = tcase_create ("Set, get, duplicate string value");
    tcase_add_test (tc, test_string);
    suite_add_tcase (s, tc);

    tc = tcase_create ("Set, get integer value");
    tcase_add_test (tc, test_int);
    suite_add_tcase (s, tc);

    tc = tcase_create ("Set, get unsigned integer value");
    tcase_add_test (tc, test_uint);
    suite_add_tcase (s, tc);

    tc = tcase_create ("Set, get boolean value");
    tcase_add_test (tc, test_bool);
    suite_add_tcase (s, tc);

    tc = tcase_create ("Set, get pointer value");
    tcase_add_test (tc, test_pointer);
    suite_add_tcase (s, tc);

    tc = tcase_create ("To string");
    tcase_add_test (tc, test_to_string);
    suite_add_tcase (s, tc);

    sr = srunner_create (s);
    srunner_run_all (sr, CK_NORMAL);
    num_failed = srunner_ntests_failed (sr);
    srunner_free (sr);

    return num_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;	
}
