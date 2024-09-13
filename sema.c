#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>


#define CARTS 20
#define CASHIERS 3
#define TERMINALS 5
#define SCANNERS 10

// semaphores needed
sem_t cashiers[CASHIERS], carts, terminal, scanner_pickup, scanner_return;

// Mutex needed to prevent racing between threads
pthread_mutex_t cashier_queue[CASHIERS];
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t scanner_move_mutex = PTHREAD_MUTEX_INITIALIZER;


// Variable to count the queue length
int cashier_queue_lengths[CASHIERS] = {0};



// Function to handle the customer arrival, it creates a thread `customer_thread` and join to the function handle_carts()
void *handle_customers_arrival(void* arg);

// Function to handle the carts availability using a semaphore `cashier`, it creates a thread `customer_choice_thread` and joins it with the following function handle_customer_choice()
void *handle_carts(void* arg);

// Function to handle the customer choice between taking a handheld scanner oor shopping the normal way, it uses the scanner_pickup semaphore,  
// and it create a thread either to handle_self_checkout_shopping() func or handle_cashiers() func depending on the availability of the scanner_pickup semaphore
void *handle_customer_choice(void* arg);

// Function for people with handheld scanner to manage the shopping time and the self-checkout in the terminal, that uses `terminal` semaphore.
// it also has a random check for scanning all items 
// it also posts or increment the `scanner_return` semaphore, the `terminal` semaphore, and the `carts` semaphore after the customer finish shopping.
void *handle_self_checkout_shopping(void* arg);

// Function to handle the cashiers queues, it uses 3 semaphores for 3 cashiers `cashier[CASHIERS]` and uses a mutex `queue_mutex` to prevent the race condition between customers(threads),
// it also uses a mutex for each cashier `cashier_queue[CASHIERS]`
// it checks the condition if the cahier queue is long and there is a terminal free if so it creates a thread `self_checkout_thread` create a thread with handle_self_checkout_for_people_without_scanner() func 
// so that the user without hand held can skip the cashier queue and go to the self checkout terminal
// if not, it posts `cashier[cashier_id]` and the `carts` semaphores
void *handle_cashiers(void* arg);

// Function to handle people who skipped the cashier queue 
// This function is very similar to the handle_self_checkout_shopping() function but without posting the `scanners` semaphore
void *handle_self_checkout_for_people_without_scanner(void* arg);

// Function that is responsible for returning the scanners to the pickup place
// it's thread is initialized in the main function so that it checks if there are scanners independently every 5 sec
// if there is low amount of scanners in the pickup place it waits of decrement the value of the `scanner_return` and 
// post or increment the value of the `scanner_pickup` in a for loop at once
void *move_scanners(void *arg);


// Function to sleep() between min and max parameters
void wait_time(int min, int max);


int main(){
  srand(time(NULL)); // to ensure random number generating on each time the program is run 
    
    // semaphore initialisation
    sem_init(&carts, 0, CARTS);
    sem_init(&terminal, 0, TERMINALS);
    sem_init(&scanner_pickup, 0, SCANNERS);
    sem_init(&scanner_return, 0, 0);

for (int i = 0; i < CASHIERS; ++i){
    sem_init(&cashiers[i], 0, CASHIERS);
    pthread_mutex_init(&cashier_queue[i], NULL);
}
    
    // Arrival thread
    pthread_t arrival_thread;
    pthread_create(&arrival_thread, NULL, handle_customers_arrival, NULL);
    
    // scanners location managing thread
   pthread_t scanner_move_thread;
   pthread_create(&scanner_move_thread, NULL, move_scanners, NULL);

   pthread_join(arrival_thread, NULL);
   

   // destroy semaphores
    sem_destroy(&carts);
    sem_destroy(&terminal);
    sem_destroy(&scanner_pickup);
    sem_destroy(&scanner_return);

    for (int i = 0; i < CASHIERS; ++i){
    sem_destroy(&cashiers[i]);
    pthread_mutex_destroy(&cashier_queue[i]);
}
}



void wait_time(int min, int max){
    int time = min + rand() % (max - min + 1);
    sleep(time);
}



void *handle_customers_arrival(void* arg){
    int customer_id = 0;
    while(1){
        wait_time(0, 1); // Wait for a random time between 0 to 1 second for the next customer to arrive
        pthread_t customer_thread;
        pthread_create(&customer_thread, NULL, handle_carts, (void *)(long) customer_id);
        
        customer_id++;
    }
    return NULL;
}

void *handle_carts(void* arg){

int customer_id = (int)(long)arg;

if (sem_trywait(&carts) == 0){
    pthread_t customer_choice_thread;
    pthread_create(&customer_choice_thread, NULL, handle_customer_choice, (void *)(long) customer_id);
} else {
    printf("Customer %d did not find a cart and went back home crying\n", customer_id);
  }

  return NULL;

}


void *handle_customer_choice(void* arg){
  int customer_id = (int)(long) arg;
  printf("Customer %d arrived\n", customer_id);

  if( (rand() % 2 == 0) && (sem_trywait(&scanner_pickup) == 0)) {
    pthread_t terminal_thread;
    pthread_create(&terminal_thread, NULL, handle_self_checkout_shopping, (void *)(long) customer_id);
    printf("Customer %d took the handheld scanner\n", customer_id);
    
  } else {
    pthread_t cashier_thread;
    pthread_create(&cashier_thread, NULL, handle_cashiers, (void *)(long)customer_id);
    printf("Customer %d took a cart and started shopping the normal way\n", customer_id);
    sem_post(&carts);

  } 

  return NULL; 
}


void *handle_self_checkout_shopping(void* arg){
   int customer_id = (int)(long)arg;

   wait_time(8 ,12); // shopping time for handheld scanner
   
   sem_wait(&terminal);
   printf("Customer %d is in the self-checkout terminal\n", customer_id);
   // random 25% check for scanning all items
  if (rand() % 4 == 0){
    printf("Customer %d is being checked for scanning all items!!\n", customer_id);
    wait_time(8,12);  // more time taken than normal to checkout
  } else{
    wait_time(0, 1);
  }

   printf("Customer %d is finished and leaving self-checkout terminal\n", customer_id);
   sem_post(&terminal);
   sem_post(&scanner_return);
   sem_post(&carts);
   
   return NULL;
}



void *handle_cashiers(void* arg){
  int customer_id = (int)(long) arg;
  int cashier_id = 0;
  int min_queue_length = 100000;  // ensure it is more than the min queue

  wait_time(9,10); // shopping time for handheld scanner

  pthread_mutex_lock(&queue_mutex);
  for(int i = 0; i < CASHIERS; i++){
    pthread_mutex_lock(&cashier_queue[i]);
    if (cashier_queue_lengths[i] < min_queue_length){
        min_queue_length = cashier_queue_lengths[i];
        cashier_id = i;
    }
     pthread_mutex_unlock(&cashier_queue[i]);
  }

  int terminal_queue_length;   
  sem_getvalue(&terminal, &terminal_queue_length); // initialize terminal queue length with the value of the terminal semaphore

  // if the cashier queues are long and the terminals are available, choose manual check
  if (min_queue_length > 1 && terminal_queue_length > 0){
    printf("Customer %d left the cashier queue and went to the self checkout terminal\n", customer_id);
    pthread_mutex_unlock(&queue_mutex);
    pthread_t self_checkout_thread;
    pthread_create(&self_checkout_thread, NULL, handle_self_checkout_for_people_without_scanner, (void *)(long)customer_id);
  
    return NULL;
  }


  cashier_queue_lengths[cashier_id]++;
  pthread_mutex_unlock(&queue_mutex);

  sem_wait(&cashiers[cashier_id]); //decrement that cashier id
  printf("Customer %d is in cashier %d\n",customer_id, cashier_id);
  wait_time(3, 5);
  printf("Customer %d is finished checking out in cashier %d\n",customer_id, cashier_id);
  sem_post(&cashiers[cashier_id]); // increment cashier semaphore
  sem_post(&carts);

  pthread_mutex_lock(&queue_mutex);
  cashier_queue_lengths[cashier_id]--;
  pthread_mutex_unlock(&queue_mutex);

  
  return NULL;
}


void *handle_self_checkout_for_people_without_scanner(void *arg){
  int customer_id = (int)(long) arg;

  sem_wait(&terminal);
  printf("Customer %d is doing a self-checkout terminal without having a scanner\n", customer_id);
  
  // random check for people who checked out without handheld scanner 
  if ((rand() % 4) == 0){
    printf("Customer %d is being checked for scanning all items manually without having a scanner\n", customer_id);
    wait_time(8, 12);
  } else {
    wait_time(5,7);
  }

  printf("Customer %d is finished the self-checkout terminal without having a scanner, and he is leaving <3\n", customer_id);
  sem_post(&terminal);
  sem_post(&carts);

  return NULL;
}



void *move_scanners(void *arg){
  while(1) {
    sleep(5);

    int value;
    sem_getvalue(&scanner_pickup, &value);
    printf("Number of Scanners in the PICKUP Section: %d!!!!!!!!!!!!!!!!!!!!!\n", value);

    // if the value of the scanners in the pick up location decreased below 3 scanners
    if (value < 2){
      pthread_mutex_lock(&scanner_move_mutex);
      printf("To little amount of scanners in the pickup location detected!!!!\n");

      
      
      int return_value;
      sem_getvalue(&scanner_return, &return_value);
      printf("Number of Scanners in the return section: %d!!!!!!!!!!!!!!!!!!!!!\n", return_value);

      if (return_value < 2)
      {
       printf("No enough scanners in the return section to put back in the pickup location!!!!!!!!\n");
       pthread_mutex_unlock(&scanner_move_mutex);
      } else {
        wait_time(5, 6);   // time taken to return the scanners to the pickup location
      for(int i = 0; i < return_value; i++){
        sem_wait(&scanner_return);     // decrement the number of scanners in the return section
        sem_post(&scanner_pickup);    // increment the amount of the scanners in the pickup section
       
      }
       printf("Scanners moved back to the pickup location.\n");
      pthread_mutex_unlock(&scanner_move_mutex);
      }
      
    }
}
  return NULL;
}


