/*
 * Copyright 2017, Digi International Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include <libdigiapix/i2c.h>

#define I2C_TIMEOUT 1

static i2c_t *i2c_bus;
static unsigned int i2c_address;
static int eeprom_page_size, eeprom_addr_size;
static uint8_t *tx_buf;
static uint8_t *rx_buf;

/*
 * usage_and_exit() -  Show usage information and exit with 'exitval' return
 *                     value
 */
static void usage_and_exit(char *name, int exitval)
{
	fprintf(stdout,
		"Example application using libdigiapix I2C support\n"
		"\n"
		"Usage: %s <i2c-bus> <i2c-address> <address-size> <page-size> <page-index>\n\n"
		"<i2c-bus>       I2C bus index to use\n"
		"<i2c-address>   Address of the I2C EEPROM memory\n"
		"<address-size>  Number of EEPROM memory address bytes\n"
		"<page-size>     EEPROM memory page size in bytes\n"
		"<page-index>    EEPROM memory page index to use\n"
		"\n"
		"Aliases for I2C can be configured in the library config file\n"
		"\n", name);

	exit(exitval);
}

/*
 * cleanup() - Frees all the allocated memory before exiting
 */
static void cleanup(void)
{
	/* Free i2c */
	i2c_free(i2c_bus);

	/* Free buffers */
	free(rx_buf);
	free(tx_buf);
}

/**
 * sigaction_handler() - Handler to execute after receiving a signal.
 *
 * @signum: Received signal.
 */
static void sigaction_handler(int signum)
{
	/* 'atexit' executes the cleanup function */
	exit(EXIT_SUCCESS);
}

/*
 * register_signals() - Registers program signals
 */
static void register_signals(void)
{
	struct sigaction action;

	action.sa_handler = sigaction_handler;
	action.sa_flags = 0;
	sigemptyset(&action.sa_mask);

	sigaction(SIGHUP, &action, NULL);
	sigaction(SIGINT, &action, NULL);
	sigaction(SIGTERM, &action, NULL);
}

static int write_page(int page_index, uint8_t *data)
{
	unsigned int page_address = eeprom_page_size * page_index;
	int i, ret;
	uint8_t *write_data;

	/* Create write buffer */
	write_data = (uint8_t*)calloc(eeprom_page_size + eeprom_addr_size, sizeof(uint8_t));
	if (write_data == NULL) {
		printf("Error: allocating page memory\n");
		return EXIT_FAILURE;
	}

	printf("Writing %d bytes to page %d at address 0x%x...\n", eeprom_page_size,
			page_index, page_address);

	for (i = 0; i < eeprom_addr_size; i++) {
		write_data[i] = (page_address >> (8 * (eeprom_addr_size - i -1)));
	}

	/* Fill the data array. */
	for (i = 0; i < eeprom_page_size; i++) {
		write_data[(i + eeprom_addr_size)] = data[i];
	}

	if (i2c_write(i2c_bus, i2c_address, write_data, (uint16_t)(eeprom_page_size + eeprom_addr_size)) != EXIT_SUCCESS) {
		printf("Error: Data written failed.\n");
		free(write_data);
		return EXIT_FAILURE;
	}

	/* Wait for the operation to complete. (try to set I2C address offset until
	   we get a success result). */
	do {
		printf("Write in progress...\n");
		ret = i2c_write(i2c_bus, i2c_address, write_data, (uint16_t)eeprom_addr_size);
		usleep(1);
	} while (ret == EXIT_FAILURE);
	printf("Write finished!\n");

	free(write_data);

	return EXIT_SUCCESS;
}

static int read_page(int page_index, uint8_t *data)
{
	unsigned int page_address = eeprom_page_size * page_index;
	uint8_t *write_data;
	int i;

	/* Create write buffer */
	write_data = (uint8_t*)calloc(eeprom_addr_size, sizeof(uint8_t));
	if (write_data == NULL) {
		printf("Error: allocating page memory\n");
		return EXIT_FAILURE;
	}

	printf("Reading %d bytes from page %d at address 0x%x...\n", eeprom_page_size,
			page_index, page_address);

	for (i = 0; i < eeprom_addr_size; i++) {
		write_data[i] = (page_address >> (8 * (eeprom_addr_size - i -1)));
	}

	if (i2c_transfer(i2c_bus, i2c_address, write_data, (uint16_t)eeprom_addr_size,
			 data, (uint16_t)eeprom_page_size) != EXIT_SUCCESS) {
		printf("Failed to read data\n");
		free(write_data);
		return EXIT_FAILURE;
	}

	free(write_data);

	return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
	char *name = basename(argv[0]);
	int page_index;
	int i2c_bus_nb;
	int i;

	/* Check input parameters */
	if (argc != 6) {
		usage_and_exit(name, EXIT_FAILURE);
		return EXIT_FAILURE;
	}

	i2c_bus_nb = atoi(argv[1]);
	i2c_address = (unsigned int)strtol(argv[2],NULL,16);
	eeprom_addr_size = atoi(argv[3]);
	eeprom_page_size = atoi(argv[4]);
	page_index = atoi(argv[5]);

	if (eeprom_addr_size <= 0) {
		printf("Address size must be greater than 0\n");
		return EXIT_FAILURE;
	}

	if (page_index < 0) {
		printf("Page index must be 0 or greater\n");
		return EXIT_FAILURE;
	}

	if (eeprom_page_size <= 0) {
		printf("Page size must be greater than 0\n");
		return EXIT_FAILURE;
	}

	if (i2c_bus_nb < 0) {
		printf("I2C bus index must be 0 or greater\n");
		return EXIT_FAILURE;
	}

	/* Register signals and exit cleanup function */
	atexit(cleanup);
	register_signals();

	/* Request I2C */
	i2c_bus = i2c_request((unsigned int)i2c_bus_nb);
	if (!i2c_bus) {
		printf("Failed to initialize I2C\n");
		return EXIT_FAILURE;
	}

	/* Set the timeout for the I2C slave. */
	if (i2c_set_timeout(i2c_bus, I2C_TIMEOUT) != EXIT_SUCCESS) {
		printf("Failed to set I2C timeout\n");
		return EXIT_FAILURE;
	}

	printf("Preparing I2C data to write...\n");

	/* Create write buffer */
	tx_buf = (uint8_t*)calloc(eeprom_page_size + eeprom_addr_size, sizeof(uint8_t));
	if (tx_buf == NULL) {
		printf("Error: allocating page memory\n");
		return EXIT_FAILURE;
	}

	/* Erase the EEPROM page. */
	for (i = 0; i < eeprom_page_size; i++) {
		tx_buf[i] = 0xFF;
	}

	if (write_page(page_index, tx_buf) != EXIT_SUCCESS) {
		printf("Failed to erase EEPROM\n");
		return EXIT_FAILURE;
	}

	/* Create some random data that will be written to the EEPROM */
	srand(time(NULL));

	for (i = 0; i < eeprom_page_size; i++) {
		tx_buf[i] = rand() % 255;
	}

	/* Write new data */
	if (write_page(page_index, tx_buf) != EXIT_SUCCESS) {
		printf("Failed to write EEPROM\n");
		return EXIT_FAILURE;
	}

	/* Create read buffer */
	rx_buf = (uint8_t*)calloc(eeprom_page_size, sizeof(uint8_t));
	if (rx_buf == NULL) {
		printf("Error: allocating page memory\n");
		return EXIT_FAILURE;
	}

	/* Read the data back */
	if (read_page(page_index, rx_buf) != EXIT_SUCCESS) {
		printf("Failed to read EEPROM\n");
		return EXIT_FAILURE;
	}

	/* Verify the written data */
	printf("Validating read data...\n");

	for (i = 0; i < eeprom_page_size; i++) {
		printf("written_data[%d] = 0x%02x : read_data[%d] = 0x%02x\n", i,
				tx_buf[i], i, rx_buf[i]);
		if (rx_buf[i] != tx_buf[i]) {
			printf("Failed while comparing written EEPROM data.\n \
				Expected 0x%x but got 0x%x\n", tx_buf[i], rx_buf[i]);
			return EXIT_FAILURE;
		}
	}

	printf("Data has been written and verified correctly\n");

	return EXIT_SUCCESS;
}