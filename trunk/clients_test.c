/* Herve Fache

20061023 Creation
*/

#include "clients.c"

static void client_show(const void *payload, char *string) {
  const client_t *client_t = payload;

  sprintf(string, "%s://", client_t->protocol);
  if (strlen(client_t->username) != 0) {
    strcat(string, client_t->username);
    if (strlen(client_t->password) != 0) {
      strcat(string, ":");
      strcat(string, client_t->password);
    }
    strcat(string, "@");
  }
  strcat(string, client_t->hostname);
  strcat(string, " ");
  strcat(string, client_t->listfile);
}

int main(void) {
  if (clients_new()) {
    printf("Failed to create\n");
  }
  list_show(clients, NULL, client_show);
  clients_add("Smb://Myself:flesyM@myClient", "C:\\Backup\\Backup.LST");
  list_show(clients, NULL, client_show);
  clients_add("sSh://otherClient", "/home/backup/Backup.list");
  list_show(clients, NULL, client_show);
  clients_free();
  return 0;
}
