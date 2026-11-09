export interface PresetItem {
  id: string;
  name: string;
  category: "Flowchart" | "Sequence" | "Class" | "State" | "ER" | "Chart" | "New" | "Recovery";
  code: string;
  description: string;
  recommendedMode?: "strict" | "compat" | "recover";
}

export const PRESETS: PresetItem[] = [
  {
    id: "flowchart-simple",
    name: "Flowchart - Simple",
    category: "Flowchart",
    description: "Basic left-to-right flowchart with decision node and labels",
    code: `graph LR
    A[Start] --> B{Is it valid?}
    B -- Yes --> C[Process Data]
    B -- No --> D[Show Error]
    C --> E[Done]
    D --> E`,
  },
  {
    id: "flowchart-shapes",
    name: "Flowchart - Shapes & Subgraphs",
    category: "Flowchart",
    description: "Various node shapes (round, stadium, cylinder) and subgraphs",
    code: `flowchart TD
    subgraph Client ["Client Tier"]
        Browser([Web Browser])
        Mobile[/Mobile App/]
    end

    subgraph Backend ["Server Tier"]
        Gateway{API Gateway}
        Auth[[Auth Service]]
        Order[(Orders DB)]
    end

    Browser --> Gateway
    Mobile --> Gateway
    Gateway --> Auth
    Auth --> Order`,
  },
  {
    id: "sequence-auth",
    name: "Sequence - Authentication Flow",
    category: "Sequence",
    description: "Sequence diagram with actors, activation boxes, notes and loops",
    code: `sequenceDiagram
    autonumber
    actor User
    participant Client as Web App
    participant Auth as Auth Server
    participant DB as User Database

    User->>Client: Click Login
    Client->>Auth: Request Token (POST /auth)
    activate Auth
    Auth->>DB: Query User Profile
    activate DB
    DB-->>Auth: Profile OK
    deactivate DB
    Auth-->>Client: JWT Access Token
    deactivate Auth
    Note over Client,Auth: Token expires in 1 hour
    Client-->>User: Show Dashboard`,
  },
  {
    id: "class-diagram",
    name: "Class - Domain Model",
    category: "Class",
    description: "Class diagram showing inheritance, aggregation, and member visibilities",
    code: `classDiagram
    class User {
        +String id
        +String email
        -String passwordHash
        +login(password) bool
        +logout() void
    }

    class Customer {
        +List~Order~ orders
        +placeOrder(cart) Order
    }

    class Admin {
        +int permissions
        +banUser(userId) void
    }

    class Order {
        +String orderId
        +Float totalAmount
        +OrderStatus status
        +pay() void
    }

    User <|-- Customer
    User <|-- Admin
    Customer "1" *-- "*" Order : places`,
  },
  {
    id: "state-order",
    name: "State - Order Lifecycle",
    category: "State",
    description: "State transition diagram with initial, composite, and terminal states",
    code: `stateDiagram-v2
    [*] --> PendingPayment
    PendingPayment --> Paid : Payment Received
    PendingPayment --> Cancelled : Timeout / User Cancel
    
    state Processing {
        [*] --> Packing
        Packing --> Shipping : Courier Picked Up
        Shipping --> InTransit
    }

    Paid --> Processing : Prepare Items
    Processing --> Delivered : Signed by Recipient
    Delivered --> [*]
    Cancelled --> [*]`,
  },
  {
    id: "er-ecommerce",
    name: "ER - Database Schema",
    category: "ER",
    description: "Entity Relationship diagram showing primary/foreign keys and relationships",
    code: `erDiagram
    CUSTOMER ||--o{ ORDER : places
    ORDER ||--|{ ORDER_ITEM : contains
    PRODUCT ||--o{ ORDER_ITEM : ordered_in

    CUSTOMER {
        string id PK
        string name
        string email
    }

    ORDER {
        int order_number PK
        string customer_id FK
        datetime order_date
        string status
    }

    ORDER_ITEM {
        int item_id PK
        int order_number FK
        string product_id FK
        int quantity
        float unit_price
    }

    PRODUCT {
        string product_id PK
        string name
        float price
    }`,
  },
  {
    id: "pie-usage",
    name: "Pie - Browser Share",
    category: "Chart",
    description: "Pie chart visualizing proportions",
    code: `pie title Browser Market Share 2026
    "Chrome" : 65
    "Safari" : 18
    "Edge" : 8
    "Firefox" : 5
    "Other" : 4`,
  },
  {
    id: "xychart-sales",
    name: "XY Chart - Sales Trends",
    category: "Chart",
    description: "XY Chart with bars and lines over category axis",
    code: `xychart-beta
    title "Quarterly Revenue & Targets"
    x-axis [Q1, Q2, Q3, Q4]
    y-axis "Revenue (in $k)" 0 --> 120
    bar [45, 60, 75, 95]
    line [50, 65, 80, 90]`,
  },
  {
    id: "flowchart-branching",
    name: "Flowchart - Branching Logic",
    category: "Flowchart",
    description: "Flowchart with diamond decision nodes and condition branches",
    code: `graph LR
    A[Start Node] --> B{Check Condition}
    B -- Pass --> C[Approved]
    B -- Fail --> D[Rejected]
    C --> E[End]
    D --> E`,
  },
  {
    id: "new-wardley",
    name: "Wardley map",
    category: "New",
    description: "Diagram added in Mermaid 12.2",
    code: `wardley-beta
title Tea Shop
anchor Business [0.95, 0.63]
component Cup of Tea [0.79, 0.61]
component Tea [0.63, 0.81]
component Kettle [0.43, 0.35]
Business -> Cup of Tea
Cup of Tea -> Tea
Cup of Tea -> Kettle`,
  },
  {
    id: "new-cynefin",
    name: "Cynefin",
    category: "New",
    description: "Diagram added in Mermaid 12.2",
    code: `cynefin-beta
title Incident triage
complex
  "Experiment"
  "Probe"
complicated
  "Analyse"
clear
  "Follow the runbook"
chaotic
  "Stabilise"
confusion
  "Unknown"`,
  },
  {
    id: "new-railroad",
    name: "Railroad (EBNF)",
    category: "New",
    description: "Diagram added in Mermaid 12.2",
    code: `railroad-ebnf-beta
expr = term , { ( "+" | "-" ) , term } ;
term = factor , { ( "*" | "/" ) , factor } ;
factor = number | "(" , expr , ")" ;`,
  },
  {
    id: "new-swimlane",
    name: "Swimlanes",
    category: "New",
    description: "Diagram added in Mermaid 12.2",
    code: `swimlane-beta LR
  subgraph Customer
    request[Request service]
    receive[Receive update]
  end
  subgraph Support
    triage[Triage request]
    answer[Send answer]
  end
  request --> triage
  triage -->|Known issue| answer
  answer --> receive`,
  },
  {
    id: "new-usecase",
    name: "Use case",
    category: "New",
    description: "Diagram added in Mermaid 12.2",
    code: `usecase-beta
  actor Admin
  actor Person
  Checkout
  Payment
  ApplyCoupon
  Admin --|> Person
  Person --> Checkout
  Checkout ..> : include Payment
  ApplyCoupon ..> : extend Checkout`,
  },
];
